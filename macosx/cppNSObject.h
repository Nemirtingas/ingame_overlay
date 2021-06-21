#pragma once

#include "cppObjcTypes.h"

struct objcNSObject;

class cppNSObject
{
protected:
    objcNSObject* nsobject;
public:
    cppNSObject(objcNSObject* nsobject);
    cppNSObject(cppNSObject const& r);
    cppNSObject(cppNSObject&& r);
    virtual ~cppNSObject();
    
    // This can be casted to NSObject*
    inline objcNSObject* NSObject(){ return nsobject; }
    
    // Objective-C members
    bool RespondsToSelector(const char* selector);
};
