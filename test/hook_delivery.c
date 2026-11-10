/*
** Host-side test: a count hook is delivered while a long-running string or
** table library function is working, not only between Lua instructions.
**
** Each case installs a count hook whose count is far larger than the
** number of VM instructions the script executes, so any delivery seen
** during the call came from the library function itself (via
** 'lua_checkhook'). The workloads are sized to finish in well under a
** second even if no hook were ever delivered, so a regression fails
** rather than hangs.
**
** Build and run: make -C test
*/

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* Large enough that the interpreter's own count never reaches it. */
#define NEVER_BY_INSTRUCTIONS 0x7fffffff

static int deliveries;
static int raise_after;  /* 0 = never raise */

static void count_hook (lua_State *L, lua_Debug *ar) {
  (void)ar;
  deliveries++;
  if (raise_after > 0 && deliveries >= raise_after)
    luaL_error(L, "interrupted by hook");
}

static int failures;

static void check (int ok, const char *what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

/* Run 'code' with the hook armed; return the pcall status. */
static int run (lua_State *L, const char *code, int raise) {
  int status;
  deliveries = 0;
  raise_after = raise;
  lua_sethook(L, count_hook, LUA_MASKCOUNT, NEVER_BY_INSTRUCTIONS);
  status = luaL_dostring(L, code);
  lua_sethook(L, NULL, 0, 0);
  return status;
}

static void expect_delivery (lua_State *L, const char *what,
                             const char *code) {
  clock_t start = clock();
  int status = run(L, code, 0);
  double ms = 1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC;
  char line[160];
  if (status != LUA_OK) {
    printf("     error: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  snprintf(line, sizeof line, "%s (%d deliveries, %.0f ms)", what,
           deliveries, ms);
  check(status == LUA_OK && deliveries > 0, line);
}

int main (void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  /* Backtracking through nested lazy captures ('match'). */
  expect_delivery(L, "string.find, backtracking captures",
      "assert(not string.find(('a'):rep(60), ('(.-)'):rep(3) .. 'b'))");

  /* Exponential '?' backtracking ('match' recursion). */
  expect_delivery(L, "string.find, optional items",
      "assert(string.find(('a'):rep(18), ('a?'):rep(18) .. ('a'):rep(18)))");

  /* Repeated greedy expansions ('max_expand'). */
  expect_delivery(L, "string.find, greedy expansion",
      "assert(not string.find(('a'):rep(300), 'a*a*b'))");

  /* Repeated balance scans ('matchbalance'). */
  expect_delivery(L, "string.find, %b scans",
      "assert(not string.find(('('):rep(20000), '%b()'))");

  /* Back-references compare a whole capture ('match_capture'). */
  expect_delivery(L, "string.find, back-references",
      "assert(not string.find(('a'):rep(400), '(a*)%1b'))");

  /* Each character tested against a set reads up to the whole set, and a
     greedy item tests them all before it charges anything ('singlematch').
     One scan of the subject is enough to show it: no pattern item is tried
     after the first, so the only steps are the set's. */
  expect_delivery(L, "string.find, one long set",
      "local set = '[' .. ('a'):rep(4096) .. 'b]*'"
      " assert(string.find(('b'):rep(4096), set))");

  /* Parsing a set reads it too, even at the end of the subject, where
     testing a character does not ('classend'). */
  expect_delivery(L, "string.find, long sets at the end of the subject",
      "local set = ('[' .. ('a'):rep(4096) .. ']?'):rep(4)"
      " assert(string.find('', set))");

  /* gsub and gmatch share the matcher. */
  expect_delivery(L, "string.gsub, backtracking",
      "local s, n = string.gsub(('a'):rep(60), ('(.-)'):rep(3) .. 'b', '')"
      " assert(n == 0)");
  expect_delivery(L, "string.gmatch, backtracking",
      "for _ in string.gmatch(('a'):rep(60), ('(.-)'):rep(3) .. 'b') do"
      " error('matched') end");

  /* A plain search costs up to l1 * l2 byte comparisons ('lmemfind'). */
  expect_delivery(L, "string.find, plain",
      "assert(not string.find(('a'):rep(20000), ('a'):rep(10000) .. 'b',"
      " 1, true))");

  /* A sort with no comparator runs no Lua code ('auxsort'). */
  expect_delivery(L, "table.sort, default comparator",
      "local t = {} for i = 1, 20000 do t[i] = (i * 7919) % 20011 end"
      " table.sort(t)"
      " for i = 2, #t do assert(t[i - 1] <= t[i]) end");

  /* Moving nils runs no Lua code and uses no memory, over a range the
     script chooses ('tmove') or a length '__len' reports ('tinsert',
     'tremove'). */
  expect_delivery(L, "table.move, an empty range",
      "table.move({}, 1, 1000000, 1, {})");
  expect_delivery(L, "table.move, an empty range moved up",
      "table.move({}, 1, 1000000, 2)");
  expect_delivery(L, "table.insert, a long empty table",
      "local t = setmetatable({}, {__len = function () return 1000000 end})"
      " table.insert(t, 1, 0)");
  expect_delivery(L, "table.remove, a long empty table",
      "local t = setmetatable({}, {__len = function () return 1000000 end})"
      " table.remove(t, 1)");

  /* A hook that raises ends the call with its error, cleanly. */
  {
    clock_t start = clock();
    int status = run(L,
        "string.find(('a'):rep(200), ('(.-)'):rep(4) .. 'b')", 3);
    double secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    const char *msg = status != LUA_OK ? lua_tostring(L, -1) : "";
    check(status != LUA_OK && strstr(msg, "interrupted by hook") != NULL,
          "a raising hook ends string.find with its error");
    check(secs < 1.0, "... promptly");
    lua_settop(L, 0);
    check(luaL_dostring(L, "assert(('abc'):find('b') == 2)") == LUA_OK,
          "... and the state is usable afterwards");
  }

  /* string.rep of nothing returns at once, whatever the count. */
  {
    clock_t start = clock();
    int status = luaL_dostring(L,
        "assert(string.rep('', math.maxinteger) == '')"
        " assert(string.rep('', math.maxinteger, '') == '')"
        " assert(string.rep('ab', 3, '') == 'ababab')"
        " assert(string.rep('', 3, ',') == ',,')");
    double secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (status != LUA_OK) printf("     error: %s\n", lua_tostring(L, -1));
    check(status == LUA_OK && secs < 0.1, "string.rep of nothing is instant");
  }

  /* Without a hook, matching results are unchanged. */
  check(luaL_dostring(L,
      "assert(select(3, ('key = value'):find('(%w+)%s*=%s*(%w+)')) == 'key')"
      " assert(('f(a(b)c)d'):match('%b()') == '(a(b)c)')"
      " assert(('hello world'):gsub('o', '0') == 'hell0 w0rld')"
      " local n = 0 for w in ('one two three'):gmatch('%a+') do n = n + 1 end"
      " assert(n == 3)"
      " assert(('aaab'):find('a-b') == 1)"
      " assert(('xyz'):find('y', 1, true) == 2)") == LUA_OK,
      "matching results unchanged without a hook");

  lua_close(L);
  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
