/* MiniSharp - a small JSON tree (for graph files). Objects keep their key order and numbers keep their
   text, so parsing and writing back gives exactly what JavaScript's JSON.stringify gave. */
#ifndef MS_JSON_H
#define MS_JSON_H

enum { JS_NULL, JS_FALSE, JS_TRUE, JS_NUM, JS_STR, JS_ARR, JS_OBJ };

typedef struct JVal JVal;
struct JVal {
    int t;
    char* s;                    /* JS_STR: the text (UTF-8, 0-terminated, may hold no 0 bytes); JS_NUM: the number as written */
    JVal** items; int n, cap;   /* JS_ARR items; JS_OBJ values */
    char** keys;                /* JS_OBJ keys, in order */
};

JVal* json_parse(const char* text, char* err, int errlen);     /* NULL on a syntax error */
void json_free(JVal* v);
char* json_write(const JVal* v);                                /* like JSON.stringify(v) (malloc'd) */

JVal* json_new(int t);
JVal* json_str(const char* s);
JVal* json_num(double d);                                       /* written like JavaScript writes numbers */
JVal* json_get(const JVal* obj, const char* key);               /* NULL if missing or obj is not an object */
void json_set(JVal* obj, const char* key, JVal* v);             /* replaces or appends (takes v) */
void json_push(JVal* arr, JVal* v);                             /* takes v */
void json_remove_at(JVal* arr, int i);

/* conveniences: the value as JavaScript's String(v) would give it (NULL for missing / null),
   and truthiness like JavaScript's !!v */
const char* json_text(const JVal* v, char* buf, int cap);
int json_truthy(const JVal* v);
double json_number(const JVal* v, double dflt);

#endif
