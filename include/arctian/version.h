#ifndef ARCTIAN_VERSION_H
#define ARCTIAN_VERSION_H

/* Tek kaynak: version.json (Makefile derleme aninda okur). */
#ifndef ARCTIAN_VERSION_STR
#define ARCTIAN_VERSION_STR "0.0"
#endif
#define ARCTIAN_VERSION ARCTIAN_VERSION_STR

/* Guncelleme manifesti icin varsayilan DuckStore sunucusu.
 * Derleme zamaninda -DARCTIAN_UPDATE_HOST=... -DARCTIAN_UPDATE_PORT=...
 * ile degistirilebilir (test icin: 10.0.2.2 8091). */
#ifndef ARCTIAN_UPDATE_HOST
#define ARCTIAN_UPDATE_HOST 45.143.99.75
#endif
#ifndef ARCTIAN_UPDATE_PORT
#define ARCTIAN_UPDATE_PORT 8090
#endif

#define ARCTIAN_STR_(x) #x
#define ARCTIAN_STR(x)  ARCTIAN_STR_(x)
#define ARCTIAN_UPDATE_URL \
    "http://" ARCTIAN_STR(ARCTIAN_UPDATE_HOST) ":" ARCTIAN_STR(ARCTIAN_UPDATE_PORT)

#endif /* ARCTIAN_VERSION_H */
