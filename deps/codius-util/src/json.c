//------------------------------------------------------------------------------
/*
    This file is part of Codius: https://github.com/codius
    Copyright (c) 2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#define JSON_TOKENS 256

#include "jsmn.h"
#include "codius-util.h"

int json_token_streq(char *js, jsmntok_t *t, char *s)
{
    return (strncmp(js + t->start, s, t->end - t->start) == 0
            && strlen(s) == (size_t) (t->end - t->start));
}

jsmntok_t *json_tokenise(char *js, size_t len)
{
    jsmn_parser parser;
    jsmn_init(&parser);

    unsigned int n = JSON_TOKENS;
    jsmntok_t *tokens = malloc(sizeof(jsmntok_t) * n);
    assert(tokens != NULL);

    int ret = jsmn_parse(&parser, js, len, tokens, n);

    while (ret == JSMN_ERROR_NOMEM)
    {
        n = n * 2 + 1;
        tokens = realloc(tokens, sizeof(jsmntok_t) * n);
        assert(tokens != NULL);
        ret = jsmn_parse(&parser, js, len, tokens, n);
    }

    if (ret == JSMN_ERROR_INVAL) {
        printf("jsmn_parse: invalid JSON string");
        abort();
    }
    if (ret == JSMN_ERROR_PART) {
        printf("jsmn_parse: truncated JSON string");
        abort();
    }

    return tokens;
}

jsmntok_t codius_json_find_token(char *js, size_t len, jsmntok_t* tokens, const char *field_name) {
  typedef enum { START, KEY, RETVAL, SKIP, STOP } parse_state;
  parse_state state = START;

  size_t object_tokens = 0;
  size_t i, j;

  for (i = 0, j = 1; j > 0; i++, j--) {
    jsmntok_t * t = &tokens[i];

    // Should never reach uninitialized tokens
    assert(t->start != -1 && t->end != -1);

    if (t->type == JSMN_ARRAY || t->type == JSMN_OBJECT)
      j += t->size;

    switch (state) {
      case START:
        if (t->type != JSMN_OBJECT) {
          printf("Invalid response: root element must be an object.");
          abort();
        }

        state = KEY;
        object_tokens = t->size;

        if (object_tokens == 0)
          state = STOP;

        if (object_tokens % 2 != 0) {
          printf("Invalid response: object must have even number of children.");
          abort();
        }

        break;

      case KEY:
        object_tokens--;

        if (t->type != JSMN_STRING) {
          printf("Invalid response: object keys must be strings.");
          abort();
        }

        state = SKIP;

        if (json_token_streq(js, t, field_name)) {
          state = RETVAL;
        }

        break;

      case SKIP:
        if (t->type != JSMN_STRING && t->type != JSMN_PRIMITIVE) {
          printf("Invalid response: object values must be strings or primitives.");
          abort();
        }

        object_tokens--;
        state = KEY;

        if (object_tokens == 0)
          state = STOP;

        break;

      case RETVAL:
        return *t;

      case STOP:
        // Just consume the tokens
        break;

      default:
        printf("Invalid state %u", state);
        abort();
    }
  }
  
  // token not found
  jsmntok_t niltoken = { 0 };
  return niltoken;
}

jsmntype_t codius_parse_json_type(char *js, size_t len, const char *field_name) {
  if (len==0 || js==NULL) return -1;

  jsmntok_t *tokens = json_tokenise(js, len);
  
  typedef enum { START, KEY, RETVAL, SKIP, STOP } parse_state;
  parse_state state = START;

  size_t object_tokens = 0;
  size_t i, j;

  jsmntok_t t = codius_json_find_token(js, len, tokens, "result");

  if (t.start == 0 && t.end == 0) {
    printf("Invalid response: token '%s' not found.", field_name);
    abort();
  }

  jsmntype_t type = t.type;

  free(tokens);
  
  return type;
}

int codius_parse_json_int(char *js, size_t len, const char *field_name) {
  if (len==0 || js==NULL) return -1;

  jsmntok_t *tokens = json_tokenise(js, len);

  jsmntok_t t = codius_json_find_token(js, len, tokens, field_name);

  if (t.start == 0 && t.end == 0) {
    printf("Invalid response: token '%s' not found.", field_name);
    abort();
  }

  if (t.type != JSMN_PRIMITIVE) {
    printf("Invalid response: object values must be primitives.");
    abort();
  }

  char *end;
  long int value = strtol(js+t.start, &end, 10);
  if (end != (js+t.end)) {
    printf("Invalid response: response is not an integer.");
    abort();
  }
  
  free(tokens);
  
  return value;
}

// Returns buf length or -1 for error.
int codius_parse_json_str(char *js, size_t len, const char *field_name, char *buf, size_t buf_size) {
  if (len==0 || js==NULL) return -1;
  
  jsmntok_t *tokens = json_tokenise(js, len);

  jsmntok_t t = codius_json_find_token(js, len, tokens, field_name);

  if (t.start == 0 && t.end == 0) {
    printf("Invalid response: token '%s' not found.\n", field_name);
    abort();
  }
  
  if (t.type != JSMN_STRING) {
    printf("Invalid response: object values must be strings or primitives.\n");
    abort();
  }

  if (buf_size < t.end-t.start) {
    printf("Insufficient buffer size: cannot copy %d byte message into %d byte buffer.\n", t.end-t.start, buf_size);
    abort();
  }
  strncpy(buf, js+t.start, t.end-t.start);

  free(tokens);

  return t.end-t.start;
}

// Returns -1 for error.
int codius_parse_json_tm(char *js, size_t len, const char *field_name, struct tm *t) {
  if (len==0 || js==NULL) return -1;

  jsmntok_t *tokens = json_tokenise(js, len);

  jsmntok_t token = codius_json_find_token(js, len, tokens, field_name);

  if (token.start == 0 && token.end == 0) {
    printf("Invalid response: token '%s' not found.", field_name);
    fflush(stdout);
    abort();
  }

  if (token.type != JSMN_OBJECT) {
    printf("Invalid response: field value must be an object.");
    fflush(stdout);
    abort();
  }

  char *field = js+token.start;
  t->tm_sec = codius_parse_json_int(field, token.end-token.start, "tm_sec");
  t->tm_min = codius_parse_json_int(field, token.end-token.start, "tm_min");
  t->tm_hour = codius_parse_json_int(field, token.end-token.start, "tm_hour");
  t->tm_mday = codius_parse_json_int(field, token.end-token.start, "tm_mday");
  t->tm_mon = codius_parse_json_int(field, token.end-token.start, "tm_mon");
  t->tm_year = codius_parse_json_int(field, token.end-token.start, "tm_year");
  t->tm_wday = codius_parse_json_int(field, token.end-token.start, "tm_wday");
  t->tm_yday = codius_parse_json_int(field, token.end-token.start, "tm_yday");
  t->tm_isdst = codius_parse_json_int(field, token.end-token.start, "tm_isdst");
  t->tm_gmtoff = codius_parse_json_int(field, token.end-token.start, "tm_gmtoff");
  
  //TODO-CODIUS: Get "tm_zone" string.

  free(tokens);
  
  return 0;
}