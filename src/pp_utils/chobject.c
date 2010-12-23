#include "chobject.h"
#include <stdlib.h>

struct CHObject *chobject_alloc() {
    return malloc(sizeof CHObject);
}

struct CHObject *chobject_init(struct CHObject *obj) {
    CAST(obj, CHObject).signature='chob';
    return obj;
}

void chobject_free(struct CHObject *obj) {
    free(obj);
}

