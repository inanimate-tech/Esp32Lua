/*
** Host test for the C-stack limit check (LUAI_CSTACKRESERVE in
** src/lua/llimits.h). test/run.sh builds it with 'luai_cstacklimit'
** pointed at test_cstacklimit(), so each case can give the interpreter a
** C stack of a chosen size below the point where it starts, as a small
** RTOS task would have; and again with TEST_NO_LIMIT, where no limit is
** known and LUAI_MAXCCALLS alone applies, as on a host.
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "llimits.h"  /* LUAI_CSTACKRESERVE */
#include "lualib.h"

static const char *g_limit = NULL;  /* lowest usable C-stack address */
static uintptr_t g_deepest = UINTPTR_MAX;  /* lowest 'reenter' frame */
static int g_calls = 0;  /* calls to 'reenter' */
static int g_failures = 0;

const char *test_cstacklimit (void) {
  return g_limit;
}

/* Gives the interpreter 'budget' bytes of C stack below the caller. */
static void setbudget (size_t budget) {
  char here;
  g_limit = (const char *)((uintptr_t)&here - budget);
  g_deepest = UINTPTR_MAX;
  g_calls = 0;
}

/* reenter(f, ...): calls f(...) from C, so a recursive f nests C calls. */
static int reenter (lua_State *L) {
  char here;
  g_calls++;
  if ((uintptr_t)&here < g_deepest)
    g_deepest = (uintptr_t)&here;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

/*
** otherstack(): leaves the interpreter holding the C-stack floor of a
** nearly full stack, as entering Lua from another task would, then gives
** this stack its room back without entering Lua again, so the floor is
** stale and above the running code.
*/
static int otherstack (lua_State *L) {
  char here;
  const char *roomy = g_limit;
  lua_State *co = lua_newthread(L);
  int nres;
  g_limit = (const char *)((uintptr_t)&here - 64);  /* nearly full */
  lua_pushcfunction(co, reenter);
  (void)lua_resume(co, NULL, 0, &nres);  /* refreshes the floor; refused */
  g_limit = roomy;
  lua_pop(L, 1);
  return 0;
}

static void check (int ok, const char *what, const char *detail) {
  printf("%s %s%s%.200s\n", ok ? "ok  " : "FAIL", what, detail ? ": " : "",
         detail ? detail : "");
  if (!ok)
    g_failures++;
}

/*
** Runs 'code', which returns a boolean and a message, and checks that it
** reports an error whose message contains 'expect'.
*/
static void expect_error (lua_State *L, const char *what, const char *code,
                          const char *expect) {
  int top = lua_gettop(L);
  if (luaL_dostring(L, code) != LUA_OK) {
    check(0, what, lua_tostring(L, -1));
  }
  else {
    int ok = lua_toboolean(L, top + 1);
    const char *msg = lua_tostring(L, top + 2);
    check(!ok && msg != NULL && strstr(msg, expect) != NULL, what,
          msg ? msg : "(no message)");
  }
  lua_settop(L, top);
}

/* Runs 'code', which must return a true first value. */
static void expect_ok (lua_State *L, const char *what, const char *code) {
  int top = lua_gettop(L);
  if (luaL_dostring(L, code) != LUA_OK)
    check(0, what, lua_tostring(L, -1));
  else
    check(lua_toboolean(L, top + 1), what, lua_tostring(L, top + 2));
  lua_settop(L, top);
}

/*
** With a C-stack limit known, 'code' must stop with an error containing
** 'expect'. With none known the same code is within LUAI_MAXCCALLS and
** must run, which shows that it was the C stack that stopped it.
*/
static void expect_limited (lua_State *L, const char *what, const char *code,
                            const char *expect) {
#if defined(TEST_NO_LIMIT)
  char runs[128];
  (void)expect;
  snprintf(runs, sizeof(runs), "without a limit, it runs (%s)", what);
  expect_ok(L, runs, code);
#else
  expect_error(L, what, code, expect);
#endif
}

/* The recursion stopped on the C stack, before reaching the reserve. */
static void check_reserve_kept (const char *what) {
#if !defined(TEST_NO_LIMIT)
  char detail[96];
  uintptr_t floor = (uintptr_t)g_limit + LUAI_CSTACKRESERVE;
  snprintf(detail, sizeof(detail), "%ld bytes above the reserve after %d "
           "levels", (long)(g_deepest - floor), g_calls);
  check(g_deepest != UINTPTR_MAX && g_deepest >= floor &&
        g_calls < LUAI_MAXCCALLS - 10, what, detail);
#else
  (void)what;
#endif
}

#define RECURSE "local function down(n) return reenter(down, n + 1) end "

int main (void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  lua_register(L, "reenter", reenter);
  lua_register(L, "otherstack", otherstack);

  setbudget(48 * 1024);
  expect_error(L, "recursion through C stops with a clean error",
               RECURSE "return pcall(down, 0)", "C stack overflow");
  check_reserve_kept("it stops on the C stack, outside the reserve");

  setbudget(48 * 1024);
  expect_error(L, "a message handler still runs after the error",
               RECURSE "return xpcall(down, debug.traceback, 0)",
               "stack traceback");

  setbudget(48 * 1024);
  expect_error(L, "the state is usable again after the error",
               RECURSE "local ok = pcall(down, 0) return pcall(down, 0)",
               "C stack overflow");

  setbudget(48 * 1024);
  expect_ok(L, "shallow recursion through C is unaffected",
            "local function down(n) "
            "  if n == 20 then return true end "
            "  return reenter(down, n + 1) end "
            "return pcall(down, 0)");

  setbudget(48 * 1024);
  expect_ok(L, "a floor cached for another stack raises no error by itself",
            "otherstack() "
            "local function down(n) "
            "  if n == 20 then return true end "
            "  return reenter(down, n + 1) end "
            "return pcall(down, 0)");

  setbudget(48 * 1024);
  expect_ok(L, "nor does it refuse to resume a coroutine",
            "otherstack() "
            "return coroutine.wrap(function () return true end)()");

  /* A finalizer enters Lua through no call of the API's: the collector
     calls it. The floor here is one cached for a much deeper stack. */
  lua_gc(L, LUA_GCSTOP);
  setbudget(8 * 1024 * 1024);
  expect_ok(L, "(an object with a recursive finalizer)",
            RECURSE
            "setmetatable({}, {__gc = function () down(0) end}) "
            "return true");
  setbudget(48 * 1024);
  lua_gc(L, LUA_GCCOLLECT);
  lua_gc(L, LUA_GCRESTART);
  check(g_calls > 0, "a finalizer run from outside Lua recurses", NULL);
  check_reserve_kept("it stops on the C stack, outside the reserve");

  /* An iterator outlives the entry it was made in, and its matcher must
     use the floor of the stack it runs on, not of the one it was made on. */
  setbudget(8 * 1024 * 1024);
  expect_ok(L, "(a gmatch iterator made on a deeper stack)",
            "iterator = string.gmatch(string.rep('a', 190), "
            "                         string.rep('a?', 190)) "
            "return true");
  setbudget(12 * 1024);
  expect_limited(L, "a gmatch iterator stops on the stack it runs on",
                 "return pcall(iterator)", "C stack overflow");

  setbudget(48 * 1024);
  expect_error(L, "resuming coroutines stops with a clean error",
               "local function down(n) return coroutine.wrap(down)(n + 1) end "
               "return pcall(down, 0)",
               "C stack overflow");

  setbudget(16 * 1024);
  expect_limited(L, "a deeply nested expression is refused cleanly",
                 "local src = 'return ' .. string.rep('(', 190) .. '1' .. "
                 "  string.rep(')', 190) "
                 "local f, err = load(src) "
                 "return f ~= nil, err",
                 "C stack overflow");

  setbudget(12 * 1024);
  expect_limited(L, "pattern matching stops with a clean error",
                 "local s = string.rep('a', 190) "
                 "return pcall(string.find, s, string.rep('a?', 190))",
                 "C stack overflow");

  lua_close(L);
  printf("%s\n", g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
