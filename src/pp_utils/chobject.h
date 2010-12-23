#ifndef __CHOBJECT_H__
#define __CHOBJECT_H__

#define CHOBJECT_HEADER int signature;
#define CAST(obj, type) ((struct type)obj)
#define DEFINE_CLASS(classname, parentclass, signature) \
    static long _class_signature=signature; \
    static struct classname##_##alloc (*_alloc_function)(void *) = classname##_##alloc; \
    static struct parentclass##_##init (*_parent_init_function)(void *) = parentclass##_##init;

    Jpeg, Image, 'jpeg');

#define INIT_OBJECT(obj) \
    obj = _parent_init_function(obj);
    CAST(

struct CHObject {
    CHOBJECT_HEADER
    int padding[4];
};

struct CHObject *chobject_alloc();
struct CHObject *chobject_init(struct CHObject *obj);



#endif //__CHOBJECT_H__
