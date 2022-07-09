#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long isPow(unsigned long x)
{
    unsigned long l = 0;
    while (x > 1)
    {
        x >>= 1;
        l++;
    }
    return l;
}

typedef enum assoc
{
    direct,
    nWay,
    fully
} assoc;

typedef enum replacementPolicy
{
    fifo,
    lru
} replacementPolicy;

typedef struct cachePref
{
    unsigned long size, n, b, blockSize, tag, sets, offset;
    replacementPolicy replacementPolicy;
    assoc assoc;
} cachePref;

typedef struct cLine
{
    bool valid;
    unsigned long tag, age;
} cLine;

typedef struct cSet
{
    cLine *lines;
} cSet;

typedef struct Cache
{
    unsigned long reads, writes, hits, misses;
    bool prftch;
    cSet *sets;
} Cache;

void start(Cache *cache, cachePref *cacheConfig)
{
    cache->reads = 0;
    cache->writes = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->sets = NULL;

    cache->sets = (cSet *)malloc(cacheConfig->sets * sizeof(cSet));
    cSet *cacheSet;
    for (int i = 0; i < cacheConfig->sets; i++)
    {
        cacheSet = &(cache->sets[i]);
        cacheSet->lines = (cLine *)malloc(cacheConfig->n * sizeof(cLine));
        for (int j = 0; j < cacheConfig->n; j++)
        {
            cacheSet->lines[j].tag = 0;
            cacheSet->lines[j].age = 0;
            cacheSet->lines[j].valid = false;
        }
    }
}

bool search(Cache *cache, cachePref *cachePrefs, unsigned long address, bool prftch)
{
    unsigned long idx = (address & (((1ul << cachePrefs->b) - 1) << cachePrefs->offset)) >> cachePrefs->offset;
    unsigned long tag = (address & (((1ul << cachePrefs->tag) - 1) << (cachePrefs->offset + cachePrefs->b))) >> (cachePrefs->offset + cachePrefs->b);

    if (cachePrefs->assoc == direct)
    {
        cLine *check = &(cache->sets[0].lines[idx]);
        if (check->valid && check->tag == tag)
        {
            return true;
        }

        check->valid = true;
        check->tag = tag;
    }
    else
    {
        unsigned long linesId = cachePrefs->n;
        cLine *last = NULL;
        cSet *cSet = &(cache->sets[idx]);
        cLine *line = NULL;

        for (int i = 0; i < cachePrefs->n; ++i)
        {
            line = &(cSet->lines[i]);

            if (line->valid)
            {
                if (line->tag == tag)
                {
                    if (cachePrefs->replacementPolicy == lru && !prftch)
                    {

                        line->age = 0;
                        cLine *temp = NULL;
                        for (int i = 0; i < cachePrefs->n; ++i)
                        {
                            temp = &(cSet->lines[i]);
                            if (line != temp)
                                temp->age += 1;
                        }
                    }
                    return true;
                }
                else if (last == NULL || line->age > last->age)
                {
                    last = line;
                }
            }
            else if (linesId == cachePrefs->n)
            {
                linesId = i;
            }
        }

        cLine *sv = NULL;

        if (linesId == cachePrefs->n)
        {
            sv = last;
        }
        else
        {
            sv = &(cSet->lines[linesId]);
        }

        sv->valid = true;
        sv->tag = tag;
        sv->age = 0;

        cLine *temp = NULL;

        for (int i = 0; i < cachePrefs->n; i++)
        {
            temp = &(cSet->lines[i]);
            if (sv != temp)
            {
                temp->age += 1;
            }
        }
    }
    return false;
}

void clearMem(Cache *cache, cachePref *cacheConfig)
{
    for (int i = 0; i < cacheConfig->sets; i++)
    {
        free(cache->sets[i].lines);
    }
    free(cache->sets);
}

int main(int argc, char **argv)
{
    if (argc < 6)
        return 0;

    FILE *fPtr = fopen(argv[5], "r");

    cachePref cacheConfig = {.size = 0, .assoc = direct, .n = 0, .replacementPolicy = fifo, .blockSize = 0};

    cacheConfig.size = atoi(argv[1]);
    cacheConfig.blockSize = atoi(argv[4]);

    if (strcmp(argv[3], "lru") == 0)
    {
        cacheConfig.replacementPolicy = lru;
    }

    if (strcmp(argv[2], "assoc") == 0)
    {
        cacheConfig.assoc = fully;
    }
    else if (strncmp(argv[2], "assoc:", strlen("assoc:")) == 0)
    {
        cacheConfig.assoc = nWay;
        cacheConfig.n = atoi(&argv[2][6]);
    }

    char command;
    unsigned long address;
    Cache nCache, pCache;

    unsigned long blocks = cacheConfig.size / cacheConfig.blockSize;

    cacheConfig.sets = 1;

    if (cacheConfig.assoc != nWay)
    {
        cacheConfig.n = blocks;
        if (cacheConfig.assoc == direct)
        {
            cacheConfig.b = isPow(blocks);
        }
        else
        {
            cacheConfig.b = 0;
        }
    }
    else
    {
        cacheConfig.sets = blocks / cacheConfig.n;
        cacheConfig.b = isPow(cacheConfig.sets);
    }

    cacheConfig.offset = isPow(cacheConfig.blockSize);
    cacheConfig.tag = 48 - cacheConfig.b - cacheConfig.offset;

    Cache *caches[2] = {&nCache, &pCache};
    start(&nCache, &cacheConfig);
    start(&pCache, &cacheConfig);

    nCache.prftch = false;
    pCache.prftch = true;

    while (fscanf(fPtr, "%*x: %c %lx ", &command, &address) > 1)
    {
        if(command=='R')
        {
            for (int i = 0; i < 2; i++)
            {
                if (search(caches[i], &cacheConfig, address, false))
                {
                    ++caches[i]->hits;
                }
                else
                {
                    ++caches[i]->misses;
                    ++caches[i]->reads;
                    if (caches[i]->prftch)
                    {
                        if (!search(caches[i], &cacheConfig, address + cacheConfig.blockSize, true))
                        {
                            ++caches[i]->reads;
                        }
                    }
                }
            }
        }
        if(command=='W')
        {
            for (int i = 0; i < 2; i++)
            {
                if (search(caches[i], &cacheConfig, address, false))
                {
                    ++caches[i]->hits;
                    ++caches[i]->writes;
                }
                else
                {
                    ++caches[i]->misses;
                    ++caches[i]->reads;
                    ++caches[i]->writes;
                    if (caches[i]->prftch)
                    {
                        if (!search(caches[i], &cacheConfig, address + cacheConfig.blockSize, true))
                        {
                            ++caches[i]->reads;
                        }
                    }
                }
            }
        }
    }

    printf("Prefetch 0");
    printf("\n");
    printf("Memory reads: %lu", nCache.reads);
    printf("\n");
    printf("Memory writes: %lu", nCache.writes);
    printf("\n");
    printf("Cache hits: %lu", nCache.hits);
    printf("\n");
    printf("Cache misses: %lu", nCache.misses);
    printf("\n");
    printf("Prefetch 1");
    printf("\n");
    printf("Memory reads: %lu", pCache.reads);
    printf("\n");
    printf("Memory writes: %lu", pCache.writes);
    printf("\n");
    printf("Cache hits: %lu", pCache.hits);
    printf("\n");
    printf("Cache misses: %lu", pCache.misses);
    printf("\n");


    for (int i = 0; i < cacheConfig.sets; i++)
    {
        free(nCache.sets[i].lines);
        free(pCache.sets[i].lines);
    }
    free(nCache.sets);
    free(pCache.sets);

    return 0;
}