#pragma once

#include <cstdint>

#define _C_ID       '@'
#define _C_CLASS    '#'
#define _C_SEL      ':'
#define _C_CHR      'c'
#define _C_UCHR     'C'
#define _C_SHT      's'
#define _C_USHT     'S'
#define _C_INT      'i'
#define _C_UINT     'I'
#define _C_LNG      'l'
#define _C_ULNG     'L'
#define _C_LNG_LNG  'q'
#define _C_ULNG_LNG 'Q'
#define _C_FLT      'f'
#define _C_DBL      'd'
#define _C_BFLD     'b'
#define _C_BOOL     'B'
#define _C_VOID     'v'
#define _C_UNDEF    '?'
#define _C_PTR      '^'
#define _C_CHARPTR  '*'
#define _C_ATOM     '%'
#define _C_ARY_B    '['
#define _C_ARY_E    ']'
#define _C_UNION_B  '('
#define _C_UNION_E  ')'
#define _C_STRUCT_B '{'
#define _C_STRUCT_E '}'
#define _C_VECTOR   '!'
#define _C_COMPLEX   'j'


struct ObjCClass;
struct ObjCObject {
    ObjCClass* objc_class;
};
typedef ObjCObject *id;

typedef id (__cdecl *IMP)(id, const char*, ...);

struct ObjCCache {
};

struct ObjCMethod {
    const char* name;
    const char *types;
    IMP imp;
};


struct ObjCMethods {
    uint32_t entrysize;
    uint32_t count;
    ObjCMethod   methods[];
};

struct ObjCProtocol {
};

struct ObjCProtocols {
    
};

struct ObjCIVar {
    uint64_t *offs;
    const char* name;
    const char* type;
    uint32_t align;
    uint32_t size;
};

struct ObjCIVars {
    uint32_t entrysize;
    uint32_t count;
    ObjCIVar ivars[];
};

struct ObjCProperty {
    const char* name;
    const char* attr;
};

struct ObjCProperties {
    uint32_t entrysize;
    uint32_t count;
    ObjCProperty properties[];
};

struct ObjCClassdata {
    uint32_t    flags;
    uint32_t    ivar_base_start;
    uint32_t    ivar_base_size;
    uint32_t    reserved;
    uint8_t    *ivar_lyt;
    const char *name;
    ObjCMethods    *base_meths;
    ObjCProtocols  *base_prots;
    ObjCIVars      *ivars;
    uint8_t*   *weak_ivar_lyt;
    ObjCProperties *base_props;
};

struct ObjCClass {
    ObjCClass     *isa;
    ObjCClass     *superclass;
    ObjCCache     *cache;
    void      *vtable;
    ObjCClassdata *info;
};
