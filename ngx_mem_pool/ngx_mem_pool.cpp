#include"ngx_mem_pool.h"
//#include"malloc.h"


//  ����ָ��size��С���ڴ�أ�����С���ڴ�ش�С������һ��ҳ���С
void* ngx_mem_pool::ngx_create_pool(size_t size) {
    pool = (ngx_pool_s*)malloc(size);//�����û�ָ����С�����ڴ��
    if (pool == nullptr) {
        return nullptr;
    }
    //  ��ʼ��ngx_pool_data_t����Ϣ
    pool->d.last = (u_char*)pool + sizeof(ngx_pool_s); //  �ڴ����ʼ
    pool->d.end = (u_char*)pool + size; //  �ڴ��ĩβ
    pool->d.next = nullptr; //  ��һ��С���ڴ�
    pool->d.failed = 0;
    //  ��ʼ���ڴ�ع�����Ա��Ϣ
    size = size - sizeof(ngx_pool_s);
    pool->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;//��ҳ���С���бȽ�

    pool->current = pool;
    pool->large = nullptr;
    pool->cleanup = nullptr;

    return pool;
}

//  �����ڴ��ֽڶ��룬���ڴ������size��С���ڴ�
void* ngx_mem_pool::ngx_palloc(size_t size) {
    if (size <= ngx_mem_pool::pool->max) { //�����ڴ�С��һ��ҳ��
        return ngx_palloc_small(size, 1);//С���ڴ����
    }
    return ngx_palloc_large(size);//����ڴ����
}

//  �������ڴ��ֽڶ��룬���ڴ������size��С���ڴ�
void* ngx_mem_pool::ngx_pnalloc(size_t size) {
    if (size <= pool->max) {
        return ngx_palloc_small(size, 0);
    }
    return ngx_palloc_large(size);
}

// ���õ���ngx_pallocʵ���ڴ���䣬���ǻ��ʼ��0
void* ngx_mem_pool::ngx_pcalloc(size_t size) {
    void* p;
    p = ngx_palloc(size);
    if (p) {
        ngx_memzero(p, size);   //buf����������
    }

    return p;
}

//  С���ڴ���䣬�ڴ�ز�����Ӳ���ϵͳ���١�align=1��ζ����Ҫ�ڴ����
void* ngx_mem_pool::ngx_palloc_small(size_t size, ngx_uint_t align) {
    u_char* m;
    ngx_pool_s* p;

    p = pool->current; //ָ��ǰ�ڴ��

    do {
        m = p->d.last; //ָ���ڴ�صĿ����ڴ���ʼλ��

        if (align) { //  ���Ҫ�����
            m = ngx_align_ptr(m, NGX_ALIGNMENT); //  �ڴ���룺��ָ�������ƽ̨��ص�4/8������
        }
        //  �ڴ�صĿ����ڴ����Ҫ������ڴ�
        if ((size_t)(p->d.end - m) >= size) {
            //  mָ��ƫ��size�ֽڣ����ڴ�ظ�Ӧ�ó�������ڴ�
            p->d.last = m + size; 

            return m;
        }
        //  �����block��ʣ��Ĳ���size����ô˳��p->d.next�����ߵ��ڶ����ڴ��block��
        p = p->d.next;

    } while (p);

    //  ��pool->current��ʼ �������� ���е�block��Ҳû�ҵ����õĿ����ڴ�
    //  ��ô��ֻ���¿���block
    return ngx_palloc_block(size);
}

//  �Ӳ���ϵͳmalloc�����µ�С���ڴ�ء�
//  ngx_palloc_small����ngx_palloc_block��ngx_palloc_block�ײ����ngx_memalign��
// ��unixƽ̨��ngx_memalign����ngx_alloc�������Ƕ�malloc��ǳ��װ��
void* ngx_mem_pool::ngx_palloc_block(size_t size) {
    u_char* m;
    size_t       psize;
    ngx_pool_s* p, * newpool;

    psize = (size_t)(pool->d.end - (u_char*)pool); //  ���С����ngx_create_pool�е�sizeһ����������һ��С���ڴ��һ����

    m = (u_char*)malloc(psize);
    if (m == nullptr) {
        return nullptr;
    }

    newpool = (ngx_pool_s*)m;

    newpool->d.end = m + psize;
    newpool->d.next = nullptr;
    newpool->d.failed = 0;

    // �����ڴ�ͷ��������Ϣ�Ĵ�С
    m += sizeof(ngx_pool_data_t); //mָ������ڴ����ʼ��ַ
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    //[m, m + size]�Ǽ��������ȥ���ڴ�
    newpool->d.last = m + size;

    //  ���ڽ��뵽���������Ȼ��ζ��֮ǰ��block�������ڴ�ʧ��
    //  ����Ҫ��ǰ����block��fail������++;
    //  ��һ��block��=������ڴ�ʧ�ܴ���
    //  ��һ��block��ʧ�ܴ���>4֮�󣬾���Ϊ���Block��ʣ���ڴ��Ѿ����٣�֮������С�ڴ�ʱ�Ͳ������Block��ʼ����
    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }

    p->d.next = newpool;

    return m;
}

//  ����ڴ����
void* ngx_mem_pool::ngx_palloc_large(size_t size) {
    void* p;
    ngx_uint_t         n;
    ngx_pool_large_s* large;

    p = malloc(size);
    if (p == nullptr) {
        return nullptr;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        //  ����������������ڴ�ؽڵ��ͷ��Ϣ��alloc==nullptr
        //  ��ζ�Ų����ngx_pool_large_tû�й���һ����ڴ档
        //  ����ֱ����������
        if (large->alloc == nullptr) { //  �ȱ�����û��free���Ĵ���ڴ�ͷ��Ϣ�ڵ�
            large->alloc = p;
            return p;
        }

        if (n++ > 3) { //�������ζ�û���ҵ����д��ڴ�ͷ��ǰ�����ڴ�ͷ�����У���ֱ��׼��ngx_palloc_small����
            break;
        }
    }

    //  ��С�ڴ������һ���ڴ��������ڴ��ͷ��Ϣ
    large = (ngx_pool_large_s*)ngx_palloc_small(sizeof(ngx_pool_large_s), 1);
    if (large == nullptr) {
        free(p);    
        return nullptr;
    }

    //  �����ڴ��ͷ�巨����large��ʼ��������
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}

//  �ͷŴ���ڴ�
void ngx_mem_pool::ngx_pfree(void* p) {
    ngx_pool_large_s* l;

    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            free(l->alloc);
            l->alloc = nullptr;
            return;
        }
    }
}
//  С���ڴ� ���ͷš�ֻ���ƶ�ָ�롣
//  ��Ϊ��ʵ�ַ�ʽ�����������ͷŲ��ˡ�����ͨ���ƶ�lastָ���������ڴ档

//  �ڴ����ú���
void ngx_mem_pool::ngx_reset_pool() {
    ngx_pool_s* p;
    ngx_pool_large_s* l;
    //�����ô��ڴ�
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            free(l->alloc);
        }
    }
    //������С�ڴ�
    /*�����ڴ�ʱ���ڶ���С�ڴ�ֻ��ͷ����Ϣ������Ҫsizeof(ͷ����Ϣ�������Ϣ)
    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char*)p + sizeof(ngx_pool_s);
        p->d.failed = 0;
    }*/

    //������һ���ڴ��
    p = pool;
    p->d.last = (u_char*)p + sizeof(ngx_pool_s);
    p->d.failed = 0;

    //�ӵڶ����ڴ�ؿ�ʼѭ�������һ���ڴ��
    for (p = p->d.next; p; p = p->d.next) {
        p->d.last = (u_char*)p + sizeof(ngx_pool_data_t);
        p->d.failed = 0;
    }

    pool->current = pool;
    pool->large = nullptr;
}

//  �ڴ�����ٺ���
void ngx_mem_pool::ngx_destroy_pool() {
    ngx_pool_s* p, * n;
    ngx_pool_large_s* l;
    ngx_pool_cleanup_s* c;

    //  �����ⲿ��Դ��Ϣͷ������ ����Ԥ���õĻص�����handler���ͷ��ⲿ��Դ��
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            c->handler(c->data);
        }
    }

    //  ��������ڴ���Ϣͷ���ͷŴ���ڴ�
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            free(l->alloc);
        }
    }

    //  ����С���ڴ���Ϣͷ���ͷ�С���ڴ�
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        free(p);
        if (n == nullptr) {
            break;
        }
    }
}

//  ���ӻص�������������
ngx_pool_cleanup_s* ngx_mem_pool::ngx_pool_cleanup_add(size_t size) {
    ngx_pool_cleanup_s* c;

    c = (ngx_pool_cleanup_s*)ngx_palloc(sizeof(ngx_pool_cleanup_s)); //��������������ͷ��Ϣ���ڴ�
    if (c == nullptr) {
        return nullptr;
    }

    if (size) {
        c->data = ngx_palloc(size);
        if (c->data == nullptr) {
            return nullptr;
        }
    }
    else {
        c->data = nullptr;
    }

    c->handler = nullptr;
    c->next = pool->cleanup;
    pool->cleanup = c;
    return c;
}