/**
*  $Id: md5lib.c,v 1.10 2008/05/12 20:51:27 carregal Exp $
*  Cryptographic and Hash functions for Lua
*  @author  Roberto Ierusalimschy
*/


#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lua.h>
#include <lauxlib.h>

#if ! defined (LUA_VERSION_NUM) || LUA_VERSION_NUM < 501
#include "compat-5.1.h"
#endif

#include "md5.h"

#define MD5_TYPE "md5_t"

/**
*  Hash function. Returns a hash for a given string.
*  @param message: arbitrary binary string.
*  @param status: optional hash context
*  @return  A 128-bit hash string, or a new hash context.
*/
static int lmd5 (lua_State *L) {
  size_t len;
  const char *message = luaL_checklstring(L, 1, &len);
  char buff[16];
  if (lua_gettop(L) < 2) {
    md5(message, len, buff);
    lua_pushlstring(L, buff, 16L);
  }
  else {
    md5_t *m;
    lua_settop(L, 2);
    if (lua_isuserdata(L, 2))
      m = luaL_checkudata(L, 2, MD5_TYPE);
    else {
      m = lua_newuserdata(L, sizeof(md5_t));
      luaL_getmetatable(L, MD5_TYPE);
      lua_setmetatable(L, -2);
      md5_init(m);
    }
    if (md5_update(m, message, len)) {
      md5_finish(m, buff);
      lua_pushlstring(L, buff, 16L);
    }
  }
  return 1;
}


static int tohex(lua_State *L) {
  size_t i, l;
  const char *str = luaL_checklstring(L, 1, &l);
  luaL_Buffer b;
  char hex[3] = {0};
  luaL_buffinit(L, &b);
  for (i = 0; i < l; ++i) {
    sprintf(hex, "%02x", str[i] & 0xff);
    luaL_addlstring(&b, hex, 2);
  }
  luaL_pushresult(&b);
  return 1;
}


static int lmd5hexa(lua_State *L) {
    lmd5(L);
    if (!lua_isuserdata(L, -1)) {
        lua_insert(L, 1);
        lua_settop(L, 1);
        return tohex(L);
    }
    return 1;
}


/**
*  X-Or. Does a bit-a-bit exclusive-or of two strings.
*  @param s1: arbitrary binary string.
*  @param s2: arbitrary binary string with same length as s1.
*  @return  a binary string with same length as s1 and s2,
*   where each bit is the exclusive-or of the corresponding bits in s1-s2.
*/
static int ex_or (lua_State *L) {
  size_t l1, l2;
  const char *s1 = luaL_checklstring(L, 1, &l1);
  const char *s2 = luaL_checklstring(L, 2, &l2);
  luaL_Buffer b;
  luaL_argcheck( L, l1 == l2, 2, "lengths must be equal" );
  luaL_buffinit(L, &b);
  while (l1--) luaL_addchar(&b, (*s1++)^(*s2++));
  luaL_pushresult(&b);
  return 1;
}


static void checkseed (lua_State *L) {
  if (lua_isnone(L, 3)) {  /* no seed? */
    time_t tm = time(NULL);  /* for `random' seed */
    lua_pushlstring(L, (char *)&tm, sizeof(tm));
  }
}


#define MAXKEY	256
#define BLOCKSIZE	16



static int initblock (lua_State *L, const char *seed, int lseed, char *block) {
  size_t lkey;
  const char *key = luaL_checklstring(L, 2, &lkey);
  if (lkey > MAXKEY)
    luaL_error(L, "key too long (> %d)", MAXKEY);
  memset(block, 0, BLOCKSIZE);
  memcpy(block, seed, lseed);
  memcpy(block+BLOCKSIZE, key, lkey);
  return (int)lkey+BLOCKSIZE;
}


static void codestream (lua_State *L, const char *msg, size_t lmsg,
                                      char *block, int lblock) {
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while (lmsg > 0) {
    char code[BLOCKSIZE];
    int i;
    md5(block, lblock, code);
    for (i=0; i<BLOCKSIZE && lmsg > 0; i++, lmsg--)
      code[i] ^= *msg++;
    luaL_addlstring(&b, code, i); 
    memcpy(block, code, i); /* update seed */
  }
  luaL_pushresult(&b);
}


static void decodestream (lua_State *L, const char *cypher, size_t lcypher,
                          char *block, int lblock) {
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while (lcypher > 0) {
    char code[BLOCKSIZE];
    int i;
    md5(block, lblock, code);  /* update seed */
    for (i=0; i<BLOCKSIZE && lcypher > 0; i++, lcypher--)
      code[i] ^= *cypher++;
    luaL_addlstring(&b, code, i); 
    memcpy(block, cypher-i, i);
  }
  luaL_pushresult(&b);
}


/**
*  Encrypts a string. Uses the hash function md5 in CFB (Cipher-feedback
*  mode).
*  @param message: arbitrary binary string to be encrypted.
*  @param key: arbitrary binary string to be used as a key.
*  @param [seed]: optional arbitrary binary string to be used as a seed.
*  if no seed is provided, the function uses the result of
*  <code>time()</code> as a seed.  
*  @return  The cyphertext (as a binary string).
*/
static int crypt (lua_State *L) {
  size_t lmsg;
  const char *msg = luaL_checklstring(L, 1, &lmsg);
  size_t lseed;
  const char *seed;
  int lblock;
  char block[BLOCKSIZE+MAXKEY];
  checkseed(L);
  seed = luaL_checklstring(L, 3, &lseed);
  if (lseed > BLOCKSIZE)
    luaL_error(L, "seed too long (> %d)", BLOCKSIZE);
  /* put seed and seed length at the beginning of result */
  block[0] = (char)lseed;
  memcpy(block+1, seed, lseed);
  lua_pushlstring(L, block, lseed+1);  /* to concat with result */
  lblock = initblock(L, seed, lseed, block);
  codestream(L, msg, lmsg, block, lblock);
  lua_concat(L, 2);
  return 1;
}


/**
*  Decrypts a string. For any message, key, and seed, we have that
*  <code>decrypt(crypt(msg, key, seed), key) == msg</code>.
*  @param cyphertext: message to be decrypted (this must be the result of
   a previous call to <code>crypt</code>.
*  @param key: arbitrary binary string to be used as a key.
*  @return  The plaintext.
*/
static int decrypt (lua_State *L) {
  size_t lcyphertext;
  const char *cyphertext = luaL_checklstring(L, 1, &lcyphertext);
  size_t lseed = cyphertext[0];
  const char *seed = cyphertext+1;
  int lblock;
  char block[BLOCKSIZE+MAXKEY];
  luaL_argcheck(L, lcyphertext >= lseed+1 && lseed <= BLOCKSIZE, 1,
                 "invalid cyphered string");
  cyphertext += lseed+1;
  lcyphertext -= lseed+1;
  lblock = initblock(L, seed, lseed, block);
  decodestream(L, cyphertext, lcyphertext, block, lblock);
  return 1;
}


/*
** Assumes the table is on top of the stack.
*/
static void set_info (lua_State *L) {
	lua_pushliteral (L, "_COPYRIGHT");
	lua_pushliteral (L, "Copyright (C) 2003 PUC-Rio");
	lua_settable (L, -3);
	lua_pushliteral (L, "_DESCRIPTION");
	lua_pushliteral (L, "Basic cryptographic facilities");
	lua_settable (L, -3);
	lua_pushliteral (L, "_VERSION");
	lua_pushliteral (L, "MD5 1.1.2");
	lua_settable (L, -3);
}


static struct luaL_Reg md5lib[] = {
  {"sum", lmd5},
  {"sumhexa", lmd5hexa},
  {"exor", ex_or},
  {"crypt", crypt},
  {"decrypt", decrypt},
  {NULL, NULL}
};


int luaopen_md5 (lua_State *L) {
  luaL_newmetatable(L, MD5_TYPE);
#if LUA_VERSION_NUM < 502
  luaL_register(L, "md5", md5lib);
#elif LUA_VERSION_NUM == 502
  luaL_newlib(L, md5lib);
#endif
  set_info (L);
  return 1;
}

