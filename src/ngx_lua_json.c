/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua.h"

#include <lauxlib.h>
#include <math.h>


#define NGX_LUA_JSON_MAX_DEPTH  64


typedef struct {
    const u_char  *pos;
    const u_char  *last;
} ngx_lua_json_parser_t;


static int ngx_lua_json_stringify(lua_State *L);
static int ngx_lua_json_parse(lua_State *L);
static void ngx_lua_json_encode_value(lua_State *L, int index,
    luaL_Buffer *buffer, ngx_uint_t depth);
static void ngx_lua_json_encode_string(lua_State *L, int index,
    luaL_Buffer *buffer);
static void ngx_lua_json_encode_number(lua_State *L, int index,
    luaL_Buffer *buffer);
static void ngx_lua_json_encode_table(lua_State *L, int index,
    luaL_Buffer *buffer, ngx_uint_t depth);
static void ngx_lua_json_encode_array(lua_State *L, int index,
    luaL_Buffer *buffer, lua_Integer len, ngx_uint_t depth);
static void ngx_lua_json_encode_object(lua_State *L, int index,
    luaL_Buffer *buffer, ngx_uint_t depth);
static ngx_uint_t ngx_lua_json_table_is_array(lua_State *L,
    int index, lua_Integer *len);
static void ngx_lua_json_parse_value(lua_State *L,
    ngx_lua_json_parser_t *parser, ngx_uint_t depth);
static void ngx_lua_json_parse_literal(lua_State *L,
    ngx_lua_json_parser_t *parser, const char *literal);
static void ngx_lua_json_parse_string(lua_State *L,
    ngx_lua_json_parser_t *parser);
static void ngx_lua_json_parse_number(lua_State *L,
    ngx_lua_json_parser_t *parser);
static void ngx_lua_json_parse_array(lua_State *L,
    ngx_lua_json_parser_t *parser, ngx_uint_t depth);
static void ngx_lua_json_parse_object(lua_State *L,
    ngx_lua_json_parser_t *parser, ngx_uint_t depth);
static void ngx_lua_json_parse_skip_space(ngx_lua_json_parser_t *parser);
static ngx_uint_t ngx_lua_json_parse_is_digit(u_char ch);
static ngx_uint_t ngx_lua_json_parse_is_hex(u_char ch);
static ngx_uint_t ngx_lua_json_parse_hex4(lua_State *L,
    ngx_lua_json_parser_t *parser);
static void ngx_lua_json_add_utf8(lua_State *L, luaL_Buffer *buffer,
    ngx_uint_t codepoint);


static char ngx_lua_json_null;


static const luaL_Reg  ngx_lua_json_methods[] = {
    { "stringify", ngx_lua_json_stringify },
    { "parse", ngx_lua_json_parse },
    { NULL, NULL }
};


void
ngx_lua_json_register(lua_State *L)
{
    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_json_methods, 0);
    lua_pushlightuserdata(L, &ngx_lua_json_null);
    lua_setfield(L, -2, "null");
    lua_setglobal(L, "JSON");
}


void
ngx_lua_json_encode(lua_State *L, int index)
{
    luaL_Buffer  buffer;

    luaL_buffinit(L, &buffer);
    ngx_lua_json_encode_value(L, index, &buffer, 0);
    luaL_pushresult(&buffer);
}


void
ngx_lua_json_decode(lua_State *L, int index)
{
    size_t                 len;
    const char            *json;
    ngx_lua_json_parser_t  parser;

    index = lua_absindex(L, index);

    json = luaL_checklstring(L, index, &len);

    parser.pos = (const u_char *) json;
    parser.last = parser.pos + len;

    ngx_lua_json_parse_skip_space(&parser);
    ngx_lua_json_parse_value(L, &parser, 0);
    ngx_lua_json_parse_skip_space(&parser);

    if (parser.pos != parser.last) {
        (void) luaL_error(L, "invalid JSON");
    }
}


static int
ngx_lua_json_stringify(lua_State *L)
{
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "JSON.stringify() takes value");
    }

    ngx_lua_json_encode(L, 1);

    return 1;
}


static int
ngx_lua_json_parse(lua_State *L)
{
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "JSON.parse() takes text");
    }

    ngx_lua_json_decode(L, 1);

    return 1;
}


static void
ngx_lua_json_encode_value(lua_State *L, int index,
    luaL_Buffer *buffer, ngx_uint_t depth)
{
    index = lua_absindex(L, index);

    if (depth > NGX_LUA_JSON_MAX_DEPTH) {
        (void) luaL_error(L, "JSON value is too deeply nested");
        return;
    }

    switch (lua_type(L, index)) {
    case LUA_TNIL:
        luaL_addstring(buffer, "null");
        return;

    case LUA_TBOOLEAN:
        luaL_addstring(buffer, lua_toboolean(L, index) ? "true" : "false");
        return;

    case LUA_TNUMBER:
        ngx_lua_json_encode_number(L, index, buffer);
        return;

    case LUA_TSTRING:
        ngx_lua_json_encode_string(L, index, buffer);
        return;

    case LUA_TTABLE:
        ngx_lua_json_encode_table(L, index, buffer, depth);
        return;

    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(L, index) == &ngx_lua_json_null) {
            luaL_addstring(buffer, "null");
            return;
        }
        break;

    default:
        break;
    }

    (void) luaL_error(L, "value cannot be encoded as JSON");
}


static void
ngx_lua_json_encode_string(lua_State *L, int index, luaL_Buffer *buffer)
{
    u_char       ch;
    size_t       i, len;
    const char  *value;
    const char   hex[] = "0123456789abcdef";
    char         escape[6];

    value = lua_tolstring(L, index, &len);

    luaL_addchar(buffer, '"');

    for (i = 0; i < len; i++) {
        ch = (u_char) value[i];

        switch (ch) {
        case '"':
            luaL_addstring(buffer, "\\\"");
            break;
        case '\\':
            luaL_addstring(buffer, "\\\\");
            break;
        case '\b':
            luaL_addstring(buffer, "\\b");
            break;
        case '\f':
            luaL_addstring(buffer, "\\f");
            break;
        case '\n':
            luaL_addstring(buffer, "\\n");
            break;
        case '\r':
            luaL_addstring(buffer, "\\r");
            break;
        case '\t':
            luaL_addstring(buffer, "\\t");
            break;
        default:
            if (ch < 0x20) {
                escape[0] = '\\';
                escape[1] = 'u';
                escape[2] = '0';
                escape[3] = '0';
                escape[4] = hex[ch >> 4];
                escape[5] = hex[ch & 0x0f];
                luaL_addlstring(buffer, escape, sizeof(escape));

            } else {
                luaL_addchar(buffer, (char) ch);
            }
        }
    }

    luaL_addchar(buffer, '"');
}


static void
ngx_lua_json_encode_number(lua_State *L, int index, luaL_Buffer *buffer)
{
    unsigned    len;
    char        value[LUA_N2SBUFFSZ];
    lua_Number  number;

    if (!lua_isinteger(L, index)) {
        number = lua_tonumber(L, index);
        if (!isfinite((double) number)) {
            (void) luaL_error(L, "number cannot be encoded as JSON");
            return;
        }
    }

    len = lua_numbertocstring(L, index, value);
    if (len == 0) {
        (void) luaL_error(L, "number cannot be encoded as JSON");
        return;
    }

    luaL_addlstring(buffer, value, len - 1);
}


static void
ngx_lua_json_encode_table(lua_State *L, int index, luaL_Buffer *buffer,
    ngx_uint_t depth)
{
    lua_Integer  len;

    index = lua_absindex(L, index);

    if (ngx_lua_json_table_is_array(L, index, &len)) {
        ngx_lua_json_encode_array(L, index, buffer, len, depth + 1);
        return;
    }

    ngx_lua_json_encode_object(L, index, buffer, depth + 1);
}


static void
ngx_lua_json_encode_array(lua_State *L, int index, luaL_Buffer *buffer,
    lua_Integer len, ngx_uint_t depth)
{
    lua_Integer  i;

    index = lua_absindex(L, index);

    luaL_addchar(buffer, '[');

    for (i = 1; i <= len; i++) {
        if (i > 1) {
            luaL_addchar(buffer, ',');
        }

        lua_rawgeti(L, index, i);
        ngx_lua_json_encode_value(L, -1, buffer, depth);
        lua_pop(L, 1);
    }

    luaL_addchar(buffer, ']');
}


static void
ngx_lua_json_encode_object(lua_State *L, int index, luaL_Buffer *buffer,
    ngx_uint_t depth)
{
    unsigned  first;

    index = lua_absindex(L, index);
    first = 1;

    luaL_addchar(buffer, '{');

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            (void) luaL_error(L, "JSON object keys must be strings");
            return;
        }

        if (!first) {
            luaL_addchar(buffer, ',');
        }
        first = 0;

        ngx_lua_json_encode_string(L, -2, buffer);
        luaL_addchar(buffer, ':');
        ngx_lua_json_encode_value(L, -1, buffer, depth);

        lua_pop(L, 1);
    }

    luaL_addchar(buffer, '}');
}


static ngx_uint_t
ngx_lua_json_table_is_array(lua_State *L, int index, lua_Integer *len)
{
    size_t       count;
    lua_Integer  key, max;

    index = lua_absindex(L, index);
    count = 0;
    max = 0;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (!lua_isinteger(L, -2)) {
            lua_pop(L, 2);
            *len = 0;
            return 0;
        }

        key = lua_tointeger(L, -2);
        if (key < 1) {
            lua_pop(L, 2);
            *len = 0;
            return 0;
        }

        count++;
        if (key > max) {
            max = key;
        }

        lua_pop(L, 1);
    }

    if (count == 0 || (lua_Integer) count != max) {
        *len = 0;
        return 0;
    }

    *len = max;
    return 1;
}


static void
ngx_lua_json_parse_value(lua_State *L, ngx_lua_json_parser_t *parser,
    ngx_uint_t depth)
{
    if (depth > NGX_LUA_JSON_MAX_DEPTH) {
        (void) luaL_error(L, "JSON value is too deeply nested");
        return;
    }

    ngx_lua_json_parse_skip_space(parser);

    if (parser->pos == parser->last) {
        (void) luaL_error(L, "invalid JSON");
        return;
    }

    switch (*parser->pos) {
    case 'n':
        ngx_lua_json_parse_literal(L, parser, "null");
        lua_pushlightuserdata(L, &ngx_lua_json_null);
        return;

    case 't':
        ngx_lua_json_parse_literal(L, parser, "true");
        lua_pushboolean(L, 1);
        return;

    case 'f':
        ngx_lua_json_parse_literal(L, parser, "false");
        lua_pushboolean(L, 0);
        return;

    case '"':
        ngx_lua_json_parse_string(L, parser);
        return;

    case '[':
        ngx_lua_json_parse_array(L, parser, depth + 1);
        return;

    case '{':
        ngx_lua_json_parse_object(L, parser, depth + 1);
        return;

    default:
        if (*parser->pos == '-' || ngx_lua_json_parse_is_digit(*parser->pos)) {
            ngx_lua_json_parse_number(L, parser);
            return;
        }
    }

    (void) luaL_error(L, "invalid JSON");
}


static void
ngx_lua_json_parse_literal(lua_State *L, ngx_lua_json_parser_t *parser,
    const char *literal)
{
    size_t  len;

    len = ngx_strlen(literal);

    if ((size_t) (parser->last - parser->pos) < len
        || ngx_strncmp(parser->pos, literal, len) != 0)
    {
        (void) luaL_error(L, "invalid JSON");
        return;
    }

    parser->pos += len;
}


static void
ngx_lua_json_parse_string(lua_State *L, ngx_lua_json_parser_t *parser)
{
    u_char       ch, escape;
    ngx_uint_t   codepoint, high, low;
    luaL_Buffer  buffer;

    if (parser->pos == parser->last || *parser->pos != '"') {
        (void) luaL_error(L, "invalid JSON string");
        return;
    }

    parser->pos++;
    luaL_buffinit(L, &buffer);

    while (parser->pos < parser->last) {
        ch = *parser->pos++;

        if (ch == '"') {
            luaL_pushresult(&buffer);
            return;
        }

        if (ch < 0x20) {
            (void) luaL_error(L, "invalid JSON string");
            return;
        }

        if (ch != '\\') {
            luaL_addchar(&buffer, (char) ch);
            continue;
        }

        if (parser->pos == parser->last) {
            (void) luaL_error(L, "invalid JSON string");
            return;
        }

        escape = *parser->pos++;

        switch (escape) {
        case '"':
        case '\\':
        case '/':
            luaL_addchar(&buffer, (char) escape);
            break;
        case 'b':
            luaL_addchar(&buffer, '\b');
            break;
        case 'f':
            luaL_addchar(&buffer, '\f');
            break;
        case 'n':
            luaL_addchar(&buffer, '\n');
            break;
        case 'r':
            luaL_addchar(&buffer, '\r');
            break;
        case 't':
            luaL_addchar(&buffer, '\t');
            break;
        case 'u':
            codepoint = ngx_lua_json_parse_hex4(L, parser);

            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                high = codepoint;

                if (parser->last - parser->pos < 6
                    || parser->pos[0] != '\\'
                    || parser->pos[1] != 'u')
                {
                    (void) luaL_error(L, "invalid JSON string");
                    return;
                }

                parser->pos += 2;
                low = ngx_lua_json_parse_hex4(L, parser);

                if (low < 0xdc00 || low > 0xdfff) {
                    (void) luaL_error(L, "invalid JSON string");
                    return;
                }

                codepoint = 0x10000 + ((high - 0xd800) << 10)
                            + (low - 0xdc00);

            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                (void) luaL_error(L, "invalid JSON string");
                return;
            }

            ngx_lua_json_add_utf8(L, &buffer, codepoint);
            break;
        default:
            (void) luaL_error(L, "invalid JSON string");
            return;
        }
    }

    (void) luaL_error(L, "invalid JSON string");
}


static void
ngx_lua_json_parse_number(lua_State *L, ngx_lua_json_parser_t *parser)
{
    size_t        len;
    const u_char *start;
    const char   *text;
    lua_Number    number;

    start = parser->pos;

    if (parser->pos < parser->last && *parser->pos == '-') {
        parser->pos++;
    }

    if (parser->pos == parser->last) {
        (void) luaL_error(L, "invalid JSON number");
        return;
    }

    if (*parser->pos == '0') {
        parser->pos++;

    } else if (*parser->pos >= '1' && *parser->pos <= '9') {
        do {
            parser->pos++;
        } while (parser->pos < parser->last
                 && ngx_lua_json_parse_is_digit(*parser->pos));

    } else {
        (void) luaL_error(L, "invalid JSON number");
        return;
    }

    if (parser->pos < parser->last && *parser->pos == '.') {
        parser->pos++;

        if (parser->pos == parser->last
            || !ngx_lua_json_parse_is_digit(*parser->pos))
        {
            (void) luaL_error(L, "invalid JSON number");
            return;
        }

        do {
            parser->pos++;
        } while (parser->pos < parser->last
                 && ngx_lua_json_parse_is_digit(*parser->pos));
    }

    if (parser->pos < parser->last
        && (*parser->pos == 'e' || *parser->pos == 'E'))
    {
        parser->pos++;

        if (parser->pos < parser->last
            && (*parser->pos == '+' || *parser->pos == '-'))
        {
            parser->pos++;
        }

        if (parser->pos == parser->last
            || !ngx_lua_json_parse_is_digit(*parser->pos))
        {
            (void) luaL_error(L, "invalid JSON number");
            return;
        }

        do {
            parser->pos++;
        } while (parser->pos < parser->last
                 && ngx_lua_json_parse_is_digit(*parser->pos));
    }

    len = parser->pos - start;

    lua_pushlstring(L, (const char *) start, len);
    text = lua_tostring(L, -1);

    if (lua_stringtonumber(L, text) != len + 1) {
        (void) luaL_error(L, "invalid JSON number");
        return;
    }

    lua_remove(L, -2);

    if (!lua_isinteger(L, -1)) {
        number = lua_tonumber(L, -1);
        if (!isfinite((double) number)) {
            (void) luaL_error(L, "invalid JSON number");
            return;
        }
    }
}


static void
ngx_lua_json_parse_array(lua_State *L, ngx_lua_json_parser_t *parser,
    ngx_uint_t depth)
{
    lua_Integer  i;

    if (parser->pos == parser->last || *parser->pos != '[') {
        (void) luaL_error(L, "invalid JSON array");
        return;
    }

    parser->pos++;
    i = 1;

    lua_newtable(L);

    ngx_lua_json_parse_skip_space(parser);

    if (parser->pos < parser->last && *parser->pos == ']') {
        parser->pos++;
        return;
    }

    for ( ;; ) {
        ngx_lua_json_parse_value(L, parser, depth);
        lua_rawseti(L, -2, i++);

        ngx_lua_json_parse_skip_space(parser);

        if (parser->pos == parser->last) {
            (void) luaL_error(L, "invalid JSON array");
            return;
        }

        if (*parser->pos == ']') {
            parser->pos++;
            return;
        }

        if (*parser->pos != ',') {
            (void) luaL_error(L, "invalid JSON array");
            return;
        }

        parser->pos++;
    }
}


static void
ngx_lua_json_parse_object(lua_State *L, ngx_lua_json_parser_t *parser,
    ngx_uint_t depth)
{
    if (parser->pos == parser->last || *parser->pos != '{') {
        (void) luaL_error(L, "invalid JSON object");
        return;
    }

    parser->pos++;
    lua_newtable(L);

    ngx_lua_json_parse_skip_space(parser);

    if (parser->pos < parser->last && *parser->pos == '}') {
        parser->pos++;
        return;
    }

    for ( ;; ) {
        ngx_lua_json_parse_skip_space(parser);

        if (parser->pos == parser->last || *parser->pos != '"') {
            (void) luaL_error(L, "invalid JSON object");
            return;
        }

        ngx_lua_json_parse_string(L, parser);

        ngx_lua_json_parse_skip_space(parser);

        if (parser->pos == parser->last || *parser->pos != ':') {
            (void) luaL_error(L, "invalid JSON object");
            return;
        }

        parser->pos++;

        ngx_lua_json_parse_value(L, parser, depth);
        lua_settable(L, -3);

        ngx_lua_json_parse_skip_space(parser);

        if (parser->pos == parser->last) {
            (void) luaL_error(L, "invalid JSON object");
            return;
        }

        if (*parser->pos == '}') {
            parser->pos++;
            return;
        }

        if (*parser->pos != ',') {
            (void) luaL_error(L, "invalid JSON object");
            return;
        }

        parser->pos++;
    }
}


static void
ngx_lua_json_parse_skip_space(ngx_lua_json_parser_t *parser)
{
    while (parser->pos < parser->last) {
        switch (*parser->pos) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            parser->pos++;
            continue;
        default:
            return;
        }
    }
}


static ngx_uint_t
ngx_lua_json_parse_is_digit(u_char ch)
{
    return ch >= '0' && ch <= '9';
}


static ngx_uint_t
ngx_lua_json_parse_is_hex(u_char ch)
{
    return (ch >= '0' && ch <= '9')
           || (ch >= 'a' && ch <= 'f')
           || (ch >= 'A' && ch <= 'F');
}


static ngx_uint_t
ngx_lua_json_parse_hex4(lua_State *L, ngx_lua_json_parser_t *parser)
{
    ngx_uint_t  codepoint, i;
    u_char      ch;

    if (parser->last - parser->pos < 4) {
        (void) luaL_error(L, "invalid JSON string");
        return 0;
    }

    codepoint = 0;

    for (i = 0; i < 4; i++) {
        ch = *parser->pos++;

        if (!ngx_lua_json_parse_is_hex(ch)) {
            (void) luaL_error(L, "invalid JSON string");
            return 0;
        }

        codepoint <<= 4;

        if (ch >= '0' && ch <= '9') {
            codepoint += ch - '0';

        } else if (ch >= 'a' && ch <= 'f') {
            codepoint += ch - 'a' + 10;

        } else {
            codepoint += ch - 'A' + 10;
        }
    }

    return codepoint;
}


static void
ngx_lua_json_add_utf8(lua_State *L, luaL_Buffer *buffer,
    ngx_uint_t codepoint)
{
    (void) L;

    if (codepoint <= 0x7f) {
        luaL_addchar(buffer, (char) codepoint);

    } else if (codepoint <= 0x7ff) {
        luaL_addchar(buffer, (char) (0xc0 | (codepoint >> 6)));
        luaL_addchar(buffer, (char) (0x80 | (codepoint & 0x3f)));

    } else if (codepoint <= 0xffff) {
        luaL_addchar(buffer, (char) (0xe0 | (codepoint >> 12)));
        luaL_addchar(buffer, (char) (0x80 | ((codepoint >> 6) & 0x3f)));
        luaL_addchar(buffer, (char) (0x80 | (codepoint & 0x3f)));

    } else if (codepoint <= 0x10ffff) {
        luaL_addchar(buffer, (char) (0xf0 | (codepoint >> 18)));
        luaL_addchar(buffer, (char) (0x80 | ((codepoint >> 12) & 0x3f)));
        luaL_addchar(buffer, (char) (0x80 | ((codepoint >> 6) & 0x3f)));
        luaL_addchar(buffer, (char) (0x80 | (codepoint & 0x3f)));

    } else {
        (void) luaL_error(L, "invalid JSON string");
    }
}
