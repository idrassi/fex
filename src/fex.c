/*
** Copyright (c) 2025 Mounir IDRASSI <mounir.idrassi@amcrypto.jp>
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fex.h"
#include "fex_span.h"

static void fex_print_line(const char *src,int ln)
{
    int i;
    const char *p = src;
    for (i=1;i<ln && *p;++p) if (*p=='\n') ++i;
    const char *line_start = p;
    while (*p && *p!='\n') ++p;
    fprintf(stderr,"%.*s", (int)(p-line_start), line_start);
}

static void fex_on_error(fe_Context *ctx,const char *msg,fe_Object *cl)
{
    int depth;
    fprintf(stderr,"error: %s\n",msg);
    for (depth=0; !fe_isnil(ctx,cl); ++depth, cl=fe_cdr(ctx,cl)) {
        const FexSpan *sp = fex_lookup_span(fe_car(ctx,cl));
        if (sp) {
            fprintf(stderr,"[%d] %s:%d:%d  =>  ",
                    depth, "<string>", sp->start_line, sp->start_col);
            fex_print_line(sp->source, sp->start_line);
            fputc('\n',stderr);
        } else {
            /* fallback – same as old printing */
            char buf[64]; fe_tostring(ctx, fe_car(ctx,cl), buf, sizeof buf);
            fprintf(stderr,"[%d] %s\n", depth, buf);
        }
    }
    exit(EXIT_FAILURE);             /* prevent double-printing in fe_error */
}


/* Convenience: span collapses to the “previous” token only ------------- */
#define CONS1(a,d) fex_cons_tok(P.ctx,(a),(d),P.previous,P.previous)
/*
================================================================================
|                              FEX BUILT-INs                                   |
================================================================================
*/

/*
 * The C implementation of our 'print' function.
 * It iterates through the argument list and prints each one,
 * separated by a space, followed by a newline.
 */
static fe_Object* builtin_print(fe_Context *ctx, fe_Object *args) {
    while (!fe_isnil(ctx, args)) {
        
        fe_Object* arg = fe_car(ctx, args);
        /* Use fe_writefp to print the fe_Object to stdout.
         * It handles all types correctly (e.g., strings are unquoted).
         */
        fe_writefp(ctx, arg, stdout);

        args = fe_cdr(ctx, args);
    }
    /* print() returns nil. */
    return fe_nil(ctx);
}

static fe_Object* builtin_println(fe_Context *ctx, fe_Object *args) {
    /* Call the print function first. */
    fe_Object* result = builtin_print(ctx, args);
    /* Then print a newline. */
    fputc('\n', stdout);
    /* Return nil as well. */
    return result;
}

/*
 * Registers all our custom C functions into the fe environment.
 */
void fex_init(fe_Context *ctx) {
    fex_init_with_config(ctx, FEX_CONFIG_NONE);
}

void fex_init_with_config(fe_Context *ctx, FexConfig config) {
    fe_handlers(ctx)->error = fex_on_error;

    /* Configure spans */
    fex_span_set_enabled(config & FEX_CONFIG_ENABLE_SPANS);

    /* Save/restore GC stack to avoid leaking the symbol object. */
    int gc_save = fe_savegc(ctx);

    /* Create the C function object and bind it to the "print" symbol. */
    fe_set(ctx,
        fe_symbol(ctx, "print"),
        fe_cfunc(ctx, builtin_print)
    );
    /* Create the C function object for println and bind it to the "println" symbol. */
    fe_set(ctx,
        fe_symbol(ctx, "println"),
        fe_cfunc(ctx, builtin_println)
    );
    
    fe_restoregc(ctx, gc_save);
}

#define fex_nil(ctx) fe_nil((ctx))

/*
================================================================================
|                                    LEXER                                     |
================================================================================
*/

typedef enum {
  /* Single-character tokens. */
  TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE, TOKEN_LBRACKET, TOKEN_RBRACKET,
  TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
  TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,

  /* One or two character tokens. */
  TOKEN_BANG, TOKEN_BANG_EQUAL,
  TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER, TOKEN_GREATER_EQUAL,
  TOKEN_LESS, TOKEN_LESS_EQUAL,

  /* Literals. */
  TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

  /* Keywords. */
  TOKEN_AND, TOKEN_ELSE, TOKEN_EXPORT, TOKEN_FALSE, TOKEN_FN, TOKEN_IF,
  TOKEN_IMPORT, TOKEN_LET, TOKEN_MODULE, TOKEN_NIL, TOKEN_OR, TOKEN_RETURN,
  TOKEN_TRUE, TOKEN_WHILE,

  TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int length;
  int line;
  int column;
} Token;

typedef struct {
  fe_Context *ctx;
  const char *start;
  const char *current;
  const char *line_start;
  int line;
  int had_error;
} Lexer;

static Lexer L;

/* Build a pair *and* attach the current token’s span ------------------- */
static fe_Object *fex_cons_tok(fe_Context *c,
                               fe_Object *car, fe_Object *cdr,
                               Token start, Token end)
{
    fe_Object *cell = fe_cons(c, car, cdr);
    fex_record_span(cell, L.start /*still points into the same buffer*/,
                    start.line, start.column,
                    end.line,   end.column);
    return cell;
}

static void init_lexer(fe_Context *ctx, const char* source) {
  L.ctx = ctx;
  L.start = source;
  L.current = source;
  L.line_start = source;
  L.line = 1;
  L.had_error = 0;
}

static int is_at_end() { return *L.current == '\0'; }

static Token make_token(TokenType type) {
  Token token;
  token.type = type;
  token.start = L.start;
  token.length = (int)(L.current - L.start);
  token.line = L.line;
  token.column = (int)(L.start - L.line_start) + 1;
  return token;
}

static Token error_token(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = L.line;
  L.had_error = 1;
  return token;
}

static char advance() {
  L.current++;
  if (L.current[-1] == '\n') { L.line++; L.line_start = L.current; }
  return L.current[-1];
}

static int match(char expected) {
  if (is_at_end()) return 0;
  if (*L.current != expected) return 0;
  L.current++;
  return 1;
}

static void skip_whitespace() {
  for (;;) {
    char c = *L.current;
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        L.line++;
        advance();
        break;
      case '/':
        if (L.current[1] == '/') {
          while (*L.current != '\n' && !is_at_end()) advance();
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static TokenType check_keyword(int start, int length, const char* rest, TokenType type) {
  if (L.current - L.start == start + length &&
      memcmp(L.start + start, rest, length) == 0) {
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static TokenType identifier_type() {
    switch (L.start[0]) {
        case 'a': return check_keyword(1, 2, "nd",  TOKEN_AND);
        case 'e':
            if (L.current - L.start > 1) {
                switch(L.start[1]) {
                    case 'l': return check_keyword(2, 2, "se", TOKEN_ELSE);
                    case 'x': return check_keyword(2, 4, "port", TOKEN_EXPORT);
                }
            }
            break;
        case 'f':
            if (L.current - L.start > 1) {
                switch (L.start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'n': if (L.current - L.start == 2) return TOKEN_FN;
                }
            }
            break;
        case 'i': if (L.current - L.start > 1 && L.start[1] == 'm') return check_keyword(1, 5, "mport", TOKEN_IMPORT); else return check_keyword(1, 1, "f", TOKEN_IF);
        case 'l': return check_keyword(1, 2, "et",  TOKEN_LET);
        case 'm': return check_keyword(1, 5, "odule", TOKEN_MODULE);
        case 'n': return check_keyword(1, 2, "il",  TOKEN_NIL);
        case 'o': return check_keyword(1, 1, "r",   TOKEN_OR);
        case 'r': return check_keyword(1, 5, "eturn",TOKEN_RETURN);
        case 't': return check_keyword(1, 3, "rue", TOKEN_TRUE);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token lex_identifier() {
  while (isalpha(*L.current) || isdigit(*L.current) || *L.current == '_') advance();
  return make_token(identifier_type());
}

static Token lex_number() {
  while (isdigit(*L.current)) advance();
  if (*L.current == '.' && isdigit(L.current[1])) {
    advance();
    while (isdigit(*L.current)) advance();
  }
  return make_token(TOKEN_NUMBER);
}

static Token lex_string() {
  while (*L.current != '"' && !is_at_end()) {
    if (*L.current == '\n') L.line++;
    advance();
  }
  if (is_at_end()) return error_token("Unterminated string.");
  advance(); /* The closing quote. */
  return make_token(TOKEN_STRING);
}

static Token scan_token() {
  skip_whitespace();
  L.start = L.current;

  if (is_at_end()) return make_token(TOKEN_EOF);

  char c = advance();
  if (isalpha(c) || c == '_') return lex_identifier();
  if (isdigit(c)) return lex_number();

  switch (c) {
    case '(': return make_token(TOKEN_LPAREN);
    case ')': return make_token(TOKEN_RPAREN);
    case '{': return make_token(TOKEN_LBRACE);
    case '}': return make_token(TOKEN_RBRACE);
    case '[': return make_token(TOKEN_LBRACKET);
    case ']': return make_token(TOKEN_RBRACKET);
    case ';': return make_token(TOKEN_SEMICOLON);
    case ',': return make_token(TOKEN_COMMA);
    case '.': return make_token(TOKEN_DOT);
    case '-': return make_token(TOKEN_MINUS);
    case '+': return make_token(TOKEN_PLUS);
    case '/': return make_token(TOKEN_SLASH);
    case '*': return make_token(TOKEN_STAR);
    case '!': return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=': return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<': return make_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>': return make_token(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"': return lex_string();
  }

  return error_token("Unexpected character.");
}

/*
================================================================================
|                            PARSER AND COMPILER                               |
================================================================================
*/

typedef struct {
    fe_Context *ctx;
    Token current;
    Token previous;
    int had_error;
    int panic_mode;
} Parser;

/* Pratt parser precedence levels */
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  /* = */
  PREC_OR,          /* or */
  PREC_AND,         /* and */
  PREC_EQUALITY,    /* == != */
  PREC_COMPARISON,  /* < > <= >= */
  PREC_TERM,        /* + - */
  PREC_FACTOR,      /* * / */
  PREC_UNARY,       /* ! - */
  PREC_CALL,        /* . () */
  PREC_PRIMARY
} Precedence;

static Parser P;

/* Forward declarations for the parser */
static fe_Object* expression();
static fe_Object* statement();
static fe_Object* declaration();
static fe_Object* parse_precedence_new(Precedence precedence);

typedef fe_Object* (*ParseFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix; /* Used as a marker for infix operators */
  Precedence precedence;
} ParseRule;

/* Function prototypes for parse rules */
static fe_Object* parse_grouping();
static fe_Object* parse_unary();
static fe_Object* parse_list();
static fe_Object* parse_number();
static fe_Object* parse_string();
static fe_Object* parse_literal();
static fe_Object* parse_variable();
static fe_Object* fn_expression();

/* A non-NULL marker to tag infix operators in the rules table. */
#define INFIX_MARKER ((ParseFn)1)

/* The rules table for the Pratt parser */
ParseRule rules[] = {
  /*                       prefix,         infix,          precedence */
  [TOKEN_LPAREN]        = {parse_grouping, INFIX_MARKER,   PREC_CALL},
  [TOKEN_RPAREN]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_LBRACE]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_RBRACE]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_LBRACKET]      = {parse_list,     NULL,           PREC_NONE},
  [TOKEN_RBRACKET]      = {NULL,           NULL,           PREC_NONE},
  [TOKEN_COMMA]         = {NULL,           NULL,           PREC_NONE},
  [TOKEN_DOT]           = {NULL,           INFIX_MARKER,   PREC_CALL},
  [TOKEN_MINUS]         = {parse_unary,    INFIX_MARKER,   PREC_TERM},
  [TOKEN_PLUS]          = {NULL,           INFIX_MARKER,   PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,           NULL,           PREC_NONE},
  [TOKEN_SLASH]         = {NULL,           INFIX_MARKER,   PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,           INFIX_MARKER,   PREC_FACTOR},
  [TOKEN_BANG]          = {parse_unary,    NULL,           PREC_UNARY},
  [TOKEN_BANG_EQUAL]    = {NULL,           INFIX_MARKER,   PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,           INFIX_MARKER,   PREC_ASSIGNMENT},
  [TOKEN_EQUAL_EQUAL]   = {NULL,           INFIX_MARKER,   PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,           INFIX_MARKER,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,           INFIX_MARKER,   PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,           INFIX_MARKER,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,           INFIX_MARKER,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {parse_variable, NULL,           PREC_NONE},
  [TOKEN_STRING]        = {parse_string,   NULL,           PREC_NONE},
  [TOKEN_NUMBER]        = {parse_number,   NULL,           PREC_NONE},
  [TOKEN_AND]           = {NULL,           INFIX_MARKER,   PREC_AND},
  [TOKEN_ELSE]          = {NULL,           NULL,           PREC_NONE},
  [TOKEN_EXPORT]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_FALSE]         = {parse_literal,  NULL,           PREC_NONE},
  [TOKEN_FN]            = {fn_expression,  NULL,           PREC_NONE},
  [TOKEN_IF]            = {NULL,           NULL,           PREC_NONE},
  [TOKEN_IMPORT]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_LET]           = {NULL,           NULL,           PREC_NONE},
  [TOKEN_MODULE]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_NIL]           = {parse_literal,  NULL,           PREC_NONE},
  [TOKEN_OR]            = {NULL,           INFIX_MARKER,   PREC_OR},
  [TOKEN_RETURN]        = {NULL,           NULL,           PREC_NONE},
  [TOKEN_TRUE]          = {parse_literal,  NULL,           PREC_NONE},
  [TOKEN_WHILE]         = {NULL,           NULL,           PREC_NONE},
  [TOKEN_ERROR]         = {NULL,           NULL,           PREC_NONE},
  [TOKEN_EOF]           = {NULL,           NULL,           PREC_NONE},
};


static void error_at(Token* token, const char* message) {
  if (P.panic_mode) return;
  P.panic_mode = 1;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  }
  else if (token->type != TOKEN_ERROR) {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }
  fprintf(stderr, ": %s\n", message);
  P.had_error = 1;
}

static void error_at_current(const char* message) { error_at(&P.current, message); }
static void error(const char* message) { error_at(&P.previous, message); }

static void parser_advance() {
    P.previous = P.current;
    for (;;) {
        P.current = scan_token();
        if (P.current.type != TOKEN_ERROR) break;
        error_at_current(P.current.start);
    }
}

static void consume(TokenType type, const char* message) {
  if (P.current.type == type) {
    parser_advance();
    return;
  }
  error_at_current(message);
}

static int check(TokenType type) { return P.current.type == type; }
static int parser_match(TokenType type) {
  if (!check(type)) return 0;
  parser_advance();
  return 1;
}

static fe_Object* symbol_from_token(Token *token) {
    char buffer[256];
    fe_Object* nil_obj = fe_nil(P.ctx);
    if (token->length >= sizeof(buffer)) {
        error("Identifier too long.");
        return nil_obj;
    }
    memcpy(buffer, token->start, token->length);
    buffer[token->length] = '\0';
    return fe_symbol(P.ctx, buffer);
}

static fe_Object* make_unary(const char* op, fe_Object* right) {
    fe_Context *c = P.ctx;
    int guard = fe_savegc(c);
    fe_pushgc(c, right);                     /* protect operand  */
    fe_Object *op_s = fe_symbol(c, op);      /* intern is GC-safe anyway */
    fe_pushgc(c, op_s);
    fe_Object *list = fe_cons(c, right, fex_nil(c));
    fe_Object *res  = CONS1(op_s, list);
    fe_restoregc(c, guard);                  /* drop temps – res survives */
    return res;
}

static fe_Object* make_binary(const char* op, fe_Object* left, fe_Object* right) {
    fe_Context *c = P.ctx;
    int guard = fe_savegc(c);
    fe_pushgc(c, left);  fe_pushgc(c, right);           /* protect args   */
    fe_Object *op_s = fe_symbol(c, op);  fe_pushgc(c, op_s);

    fe_Object *tmp  = fe_cons(c, right, fex_nil(c));
    tmp             = fe_cons(c, left,  tmp);
    fe_Object *res  = CONS1(op_s,  tmp);

    fe_restoregc(c, guard);
    return res;
}

static fe_Object* parse_grouping() {
  fe_Object* expr = expression();
  consume(TOKEN_RPAREN, "Expect ')' after expression.");
  return expr;
}

static fe_Object* parse_number() {
  double value = strtod(P.previous.start, NULL);
  /* Check if it's an integer (no decimal point) and fits in fixnum range */
  if (!memchr(P.previous.start, '.', P.previous.length) && 
      !memchr(P.previous.start, 'e', P.previous.length) && 
      !memchr(P.previous.start, 'E', P.previous.length) && 
      value >= INTPTR_MIN && value <= INTPTR_MAX && value == (intptr_t)value) {
    return fe_fixnum((intptr_t)value);
  }
  return fe_number(P.ctx, (fe_Number)value);
}

static fe_Object* parse_string() {
    char buffer[1024];
    int len = P.previous.length - 2;
    if (len < 0) len = 0;
    if (len >= sizeof(buffer)) {
        error("String too long.");
        return fe_nil(P.ctx);
    }
    memcpy(buffer, P.previous.start + 1, len);
    buffer[len] = '\0';
    return fe_string(P.ctx, buffer);
}

static fe_Object* parse_literal() {
  switch (P.previous.type) {
    case TOKEN_FALSE: return fe_bool(P.ctx, 0);
    case TOKEN_TRUE:  return fe_bool(P.ctx, 1);
    case TOKEN_NIL:   return fe_nil(P.ctx);          /* real empty list */
    default:          return fe_nil(P.ctx);          /* should not happen */
  }
}

static fe_Object* parse_variable() {
    return symbol_from_token(&P.previous);
}

static fe_Object* parse_unary() {
  TokenType operatorType = P.previous.type;
  fe_Object* right = parse_precedence_new(PREC_UNARY);
  switch (operatorType) {
    case TOKEN_MINUS: return make_unary("-", right);
    case TOKEN_BANG:  return make_unary("not", right);
    default: return fe_nil(P.ctx);
  }
}

static fe_Object* parse_precedence_new(Precedence precedence) {
    fe_Object* nil_obj = fe_nil(P.ctx);
    parser_advance();
    ParseFn prefix_rule = rules[P.previous.type].prefix;
    if (prefix_rule == NULL) {
        error("Expect expression.");
        return nil_obj;
    }
    fe_Object *left = prefix_rule();

    while (rules[P.current.type].infix != NULL &&
           precedence <= rules[P.current.type].precedence) {
        parser_advance();
        TokenType op_type = P.previous.type;
        ParseRule* rule = &rules[op_type];

        if (op_type == TOKEN_EQUAL) { /* Assignment */
            if (fe_type(P.ctx, left) != FE_TSYMBOL) {
                error("Invalid assignment target.");
                return nil_obj;
            }
            fe_Object* right = parse_precedence_new(PREC_ASSIGNMENT);
            left = make_binary("=", left, right);

        } else if (op_type == TOKEN_LPAREN) { /* Function call */
            fe_Object *nil_obj = fex_nil(P.ctx);
            fe_Object *args_head = nil_obj;
            fe_Object **args_tail = &args_head;

            if (!check(TOKEN_RPAREN)) {
                do {
                    *args_tail = fe_cons(P.ctx, expression(), nil_obj);
                    args_tail = fe_cdr_ptr(P.ctx, *args_tail);
                } while (parser_match(TOKEN_COMMA));
            }
            consume(TOKEN_RPAREN, "Expect ')' after arguments.");
            left = CONS1(left, args_head);

        } else if (op_type == TOKEN_DOT) { /* Member access */
            consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
            fe_Object* property = symbol_from_token(&P.previous);
            left = make_binary("get", left, property);
        } else { /* Standard binary operator */
            const char* op_str;
            switch(op_type) {
                case TOKEN_PLUS: op_str = "+"; break;
                case TOKEN_MINUS: op_str = "-"; break;
                case TOKEN_STAR: op_str = "*"; break;
                case TOKEN_SLASH: op_str = "/"; break;
                case TOKEN_EQUAL_EQUAL: op_str = "is"; break;
                case TOKEN_BANG_EQUAL: op_str = "not is"; break;
                case TOKEN_LESS: op_str = "<"; break;
                case TOKEN_LESS_EQUAL: op_str = "<="; break;
                case TOKEN_GREATER: op_str = ">"; break;
                case TOKEN_GREATER_EQUAL: op_str = ">="; break;
                case TOKEN_AND: op_str = "and"; break;
                case TOKEN_OR: op_str = "or"; break;
                default: error("Unhandled infix operator."); return nil_obj;
            }
            fe_Object* right = parse_precedence_new((Precedence)(rule->precedence + 1));
            
            if (op_type == TOKEN_BANG_EQUAL) {
                left = make_binary("is", left, right);
                left = make_unary("not", left);
            } else if (op_type == TOKEN_GREATER || op_type == TOKEN_GREATER_EQUAL) {
                const char* new_op = (op_type == TOKEN_GREATER) ? "<" : "<=";
                left = make_binary(new_op, right, left);
            } else {
                left = make_binary(op_str, left, right);
            }
        }
    }
    return left;
}


static fe_Object* expression() {
    return parse_precedence_new(PREC_ASSIGNMENT);
}

static fe_Object* block() {
    fe_Object* nil_obj = fex_nil(P.ctx);
    fe_Object *head = nil_obj;
    fe_Object **tail = &head;
    int count = 0;

    while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
        *tail = fe_cons(P.ctx, declaration(), nil_obj);
        tail = fe_cdr_ptr(P.ctx, *tail);
        count++;
    }
    consume(TOKEN_RBRACE, "Expect '}' after block.");

    if (count == 0) return nil_obj;
    if (count == 1) return fe_car(P.ctx, head);
    
    return CONS1(fe_symbol(P.ctx, "do"), head);
}

static fe_Object* fn_declaration() {
    fe_Object* nil_obj = fex_nil(P.ctx);
    consume(TOKEN_LPAREN, "Expect '(' after 'fn'.");
    fe_Object* params = nil_obj;
    int param_count = 0;
    if (!check(TOKEN_RPAREN)) {
        do {
            consume(TOKEN_IDENTIFIER, "Expect parameter name.");
            params = fe_cons(P.ctx, symbol_from_token(&P.previous), params);
            param_count++;
        } while (parser_match(TOKEN_COMMA));
    }
    consume(TOKEN_RPAREN, "Expect ')' after parameters.");

    fe_Object* reversed_params = nil_obj;
    int i;
    for (i=0; i<param_count; ++i) {
        reversed_params = fe_cons(P.ctx, fe_car(P.ctx, params), reversed_params);
        params = fe_cdr(P.ctx, params);
    }

    consume(TOKEN_LBRACE, "Expect '{' before function body.");
    fe_Object* body = block();

    fe_Object* list = fe_cons(P.ctx, body, nil_obj);
    list = fe_cons(P.ctx, reversed_params, list);
    return CONS1(fe_symbol(P.ctx, "fn"), list);
}

static fe_Object* fn_expression() {
    return fn_declaration();
}

static fe_Object* parse_list() {
    fe_Object *nil_obj = fex_nil(P.ctx);
    fe_Object *head = nil_obj;
    fe_Object **tail = &head;
    int gc_base = fe_savegc(P.ctx);

    if (!check(TOKEN_RBRACKET)) {
        do {
            fe_Object *elem = expression();
            fe_pushgc(P.ctx, elem);                  /* keep elem alive  */
            *tail = fe_cons(P.ctx, elem, nil_obj);
            tail  = fe_cdr_ptr(P.ctx, *tail);
            fe_restoregc(P.ctx, gc_base);            /* keep head only   */
            if (!fe_isnil(P.ctx, head)) fe_pushgc(P.ctx, head);
        } while (parser_match(TOKEN_COMMA));
    }

    consume(TOKEN_RBRACKET, "Expect ']' after list elements.");

    /* Construct the (list ...) form */
    fe_Object* list_symbol = fe_symbol(P.ctx, "list");
    return CONS1(list_symbol, head);
}

static fe_Object* module_declaration() {
    consume(TOKEN_LPAREN, "Expect '(' after 'module'.");
    consume(TOKEN_STRING, "Expect module name string.");
    fe_Object* name = parse_string(); /* This returns a fe_string object */
    consume(TOKEN_RPAREN, "Expect ')' after module name.");

    consume(TOKEN_LBRACE, "Expect '{' before module body.");
    fe_Object* body = block();

    /* Build (module name body) */
    fe_Object* list = fe_cons(P.ctx, body, fex_nil(P.ctx));
    list = fe_cons(P.ctx, name, list);
    return CONS1(fe_symbol(P.ctx, "module"), list);
}

static fe_Object* import_declaration() {
    consume(TOKEN_IDENTIFIER, "Expect module name to import.");
    fe_Object* name = symbol_from_token(&P.previous);
    consume(TOKEN_SEMICOLON, "Expect ';' after import statement.");

    /* Build (import name) */
   fe_Object* list = fe_cons(P.ctx, name, fex_nil(P.ctx));
    return CONS1(fe_symbol(P.ctx, "import"), list);
}

static fe_Object* var_declaration() {
    fe_Object* nil_obj = fex_nil(P.ctx);
    consume(TOKEN_IDENTIFIER, "Expect variable name.");
    fe_Object* name = symbol_from_token(&P.previous);
    fe_Object* value = nil_obj;
    if (parser_match(TOKEN_EQUAL)) {
        value = expression();
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    /* An l-value (the variable name) can't be nil.
       The parser creates ('=' nil value) which the 'let' primitive in fe
       would then have to handle. It's better to ensure a symbol is passed. */
    if(fe_isnil(P.ctx, name)) {
      error("Variable name cannot be nil.");
      return nil_obj;
    }

    /* `let` in the fe core is used for declaration, so we'll use that. */
    return make_binary("let", name, value);
}

static fe_Object* expr_statement() {
    fe_Object* expr = expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    return expr;
}

static fe_Object* return_statement() {
    fe_Object* nil_obj = fex_nil(P.ctx);
    fe_Object* value = nil_obj;
    if (!check(TOKEN_SEMICOLON)) {
        value = expression();
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");

    /* Build : (return <value>) */
    fe_Object* list   = fe_cons(P.ctx, value, nil_obj);
    fe_Object* r_sym  = fe_symbol(P.ctx, "return");
    return CONS1(r_sym, list);
}

static fe_Object* if_statement() {
    fe_Object* nil_obj = fex_nil(P.ctx);
    consume(TOKEN_LPAREN, "Expect '(' after 'if'.");
    fe_Object* condition = expression();
    consume(TOKEN_RPAREN, "Expect ')' after if condition.");

    fe_Object* then_branch = statement();
    fe_Object* else_branch = nil_obj;

    if (parser_match(TOKEN_ELSE)) {
        else_branch = statement();
    }
    
    fe_Object* list = fe_cons(P.ctx, else_branch, nil_obj);
    list = fe_cons(P.ctx, then_branch, list);
    list = fe_cons(P.ctx, condition, list);
    return CONS1(fe_symbol(P.ctx, "if"), list);
}

static fe_Object* while_statement() {
    fe_Object* nil_obj = fex_nil(P.ctx);
    consume(TOKEN_LPAREN, "Expect '(' after 'while'.");
    fe_Object* condition = expression();
    consume(TOKEN_RPAREN, "Expect ')' after condition.");
    fe_Object* body = statement();
    
    fe_Object* list = fe_cons(P.ctx, body, nil_obj);
    list = fe_cons(P.ctx, condition, list);
    return CONS1(fe_symbol(P.ctx, "while"), list);
}

static void synchronize() {
    P.panic_mode = 0;
    while (P.current.type != TOKEN_EOF) {
        if (P.previous.type == TOKEN_SEMICOLON) return;
        switch (P.current.type) {
            case TOKEN_FN:
            case TOKEN_LET:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;
            default:;
        }
        parser_advance();
    }
}

static fe_Object* statement() {
    if (parser_match(TOKEN_RETURN)) {
        return return_statement();
    } else if (parser_match(TOKEN_IF)) {
        return if_statement();
    } else if (parser_match(TOKEN_WHILE)) {
        return while_statement();
    } else if (parser_match(TOKEN_LBRACE)) {
        return block();
    } else {
    return expr_statement();
}
}

static fe_Object* declaration() {
    if (parser_match(TOKEN_MODULE)) {
        return module_declaration();
    }
    if (parser_match(TOKEN_IMPORT)) {
        return import_declaration();
    }

    int is_export = parser_match(TOKEN_EXPORT);

    fe_Object* decl = NULL;
    if (parser_match(TOKEN_LET)) {
        decl = var_declaration();
    } else if (parser_match(TOKEN_FN)) {
        /* This handles `fn name(...)` as a declaration */
        consume(TOKEN_IDENTIFIER, "Expect function name.");
        fe_Object* name = symbol_from_token(&P.previous);
        fe_Object* fn_expr = fn_declaration(); /* Parses `(...) { ... }` */
        decl = make_binary("let", name, fn_expr);
    }

    if (decl) {
        if (is_export) {
            return make_unary("export", decl);
        }
        return decl;
    }

    if (is_export) {
        error("Only 'let' and 'fn' declarations can be exported.");
    }

    fe_Object* stmt = statement();
    if (P.panic_mode) synchronize();
    return stmt;
}

fe_Object* fex_compile(fe_Context *ctx, const char *source) {
    init_lexer(ctx, source);

    P.ctx = ctx;
    P.had_error = 0;
    P.panic_mode = 0;
    fe_Object* nil_obj = fex_nil(P.ctx);
    
    parser_advance();

    fe_Object *head = nil_obj;
    fe_Object **tail = &head;
    int count = 0;
    int gc_base = fe_savegc(ctx);

    while (!parser_match(TOKEN_EOF)) {
        /* keep the part we already built alive */
        fe_restoregc(ctx, gc_base);
        if (!fe_isnil(ctx, head)) fe_pushgc(ctx, head);

        fe_Object *node = declaration();   /* may allocate a lot        */
        fe_pushgc(ctx, node);              /* protect it while consing  */
        *tail = fe_cons(ctx, node, nil_obj);
        tail  = fe_cdr_ptr(ctx, *tail);

        count++;
        if (P.had_error) break;
    }
    
    if (P.had_error || L.had_error) return NULL;

    fe_Object *program;
    if (count == 0)       program = nil_obj;
    else if (count == 1)  program = fe_car(ctx, head);
    else                  program = CONS1(fe_symbol(ctx, "do"), head);

    /* hand the finished AST back still rooted – caller pushes it again
       immediately, and later pops the whole frame, so no leak occurs.   */
    fe_pushgc(ctx, program);
    return program;
}

fe_Object* fex_do_string(fe_Context *ctx, const char *source) {
    int gc_save = fe_savegc(ctx);
    fe_Object* code = fex_compile(ctx, source);
    if (!code) {
        fe_restoregc(ctx, gc_save);
        return NULL;
    }
    fe_pushgc(ctx, code);
    fe_Object *res = fe_eval(ctx, code);
    fe_restoregc(ctx, gc_save);
    return res;
}
