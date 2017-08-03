/*
 * ngx_mongo.c
 *
 *  Created on: 2017年7月31日
 *      Author: root
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ucontext.h>
#include "ngx_mongo.h"

#define MAX_STACK_LEN (1<<16)

static char *ngx_http_mongo_parse(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *ngx_http_mongo_create_loc(ngx_conf_t *cf);
static char *ngx_http_mongo_merge_loc(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t ngx_http_mongo_block(ngx_http_request_t *r);

typedef struct
{
	ngx_rbtree_t root;
	ngx_rbtree_node_t sen;
	ngx_queue_t mongos;
	ngx_http_upstream_srv_conf_t *conf;
} ngx_http_mongo_loc_t;

typedef struct
{
	ngx_rbtree_node_t node;
	ngx_queue_t data;
	ucontext_t base;
	ucontext_t db_base;
	u_char buff[MAX_STACK_LEN];
	mongo db;
	ngx_connection_t *c;
	ngx_http_request_t*r;
	ngx_int_t ret;
} ngx_mongo_ctx_t;

static ngx_command_t ngx_http_mongo_commands[] =
{
		{
				ngx_string("mongo"),
				NGX_CONF_TAKE1|NGX_HTTP_LOC_CONF,
				ngx_http_mongo_parse,
				NGX_HTTP_LOC_CONF_OFFSET,
				0,
				NULL
		},
		ngx_null_command
};

static ngx_http_module_t ngx_http_mongo_ctx =
{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		ngx_http_mongo_create_loc,
		ngx_http_mongo_merge_loc
};

ngx_module_t ngx_http_mongo_module =
{
		NGX_MODULE_V1,
		&ngx_http_mongo_ctx,
		ngx_http_mongo_commands,
		NGX_HTTP_MODULE,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NGX_MODULE_V1_PADDING
};

static void ngx_mongo_event_parse(ngx_event_t *ev)
{
	ngx_connection_t *c = ev->data;
	ngx_http_request_t *r = c->data;

	r->write_event_handler(r);
}

int swap_func(mongo *pmongo)
{
	ngx_mongo_ctx_t *ctx = (ngx_mongo_ctx_t *)((char *)pmongo - offsetof(ngx_mongo_ctx_t, db));

	if (ctx->c)
	{
		swapcontext(&ctx->db_base, &ctx->base);
	}
	else
	{
		printf("##############\n");
		ctx->c = ngx_get_connection(pmongo->sock, ctx->r->connection->log);
		ctx->c->write->handler = ngx_mongo_event_parse;
		ctx->c->read->handler = ngx_mongo_event_parse;
		ctx->c->data = ctx->r;
		ngx_add_conn(ctx->c);
		swapcontext(&ctx->db_base, &ctx->base);
	}
	return 0;
}

int close_func(mongo *pmongo)
{
	printf("hare hare\n");
	return 0;
}

static void ngx_http_mongo(ngx_http_request_t *r)
{
	ngx_mongo_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_mongo_module);

	if (ctx->ret == NGX_AGAIN)
	{
		swapcontext(&ctx->base, &ctx->db_base);

		if (ctx->ret == NGX_OK)
			r->write_event_handler(r);
		return;
	}
	else if (ctx->ret == NGX_OK)
	{
		static ngx_str_t str = ngx_string("ok");
		ngx_chain_t out;
		ngx_buf_t *buf;
		ngx_str_set(&r->headers_out.content_type, "text/plain");
		r->headers_out.content_length_n = str.len;
		r->headers_out.status = NGX_HTTP_OK;

		buf = ngx_create_temp_buf(r->pool, str.len);
		ngx_memcpy(buf->last, str.data, str.len);
		buf->last += str.len;

		buf->last_buf = 1;

		out.buf = buf;
		out.next = NULL;
		ngx_http_send_header(r);
		printf("count = %d\n", r->main->count);
		ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));
	}
	else
	{
		ngx_http_finalize_request(r, NGX_ERROR);
	}

	return ;
}

static void ngx_mongo(ngx_http_request_t *r)
{
	ngx_mongo_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_mongo_module);
	bson b;
	ngx_int_t count = 0;
	mongo_init(&ctx->db);
	if (mongo_connect(&ctx->db, "127.0.0.1", 27017) != MONGO_OK)
	{
		printf("error erorr\n");
	}

	mongo_cursor *cursor = mongo_find(&ctx->db, "test.test", bson_empty(&b), NULL, 0, 0, 0);

	while (cursor && mongo_cursor_next(cursor) == MONGO_OK)
	{
		count++;
	}

	printf("count = %d\n", (int)count);
	ctx->ret = NGX_OK;
}

static void ngx_http_mongo_destroy(void *p)
{
	ngx_mongo_ctx_t *ctx = p;

	if (ctx->c)
	{
		printf("destroy destroy!\n");
		ngx_close_connection(ctx->c);
		ctx->db.sock = -1;
		mongo_destroy(&ctx->db);
	}
}

static ngx_int_t ngx_http_mongo_block(ngx_http_request_t *r)
{
	ngx_http_set_content_type(r);
	ngx_mongo_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_mongo_module);


	if (ctx == NULL)
	{
		ngx_pool_cleanup_t *cleanup = ngx_pool_cleanup_add(r->pool, sizeof(*ctx));

		ctx = cleanup->data;
		ngx_memzero(ctx, sizeof(*ctx));
		cleanup->handler = ngx_http_mongo_destroy;
		ctx->r = r;
		getcontext(&ctx->base);
		getcontext(&ctx->db_base);
		ctx->db_base.uc_link = &ctx->base;
		ctx->db_base.uc_stack.ss_size = sizeof(ctx->buff);
		ctx->db_base.uc_stack.ss_sp = ctx->buff;

		ctx->ret = NGX_AGAIN;

		makecontext(&ctx->db_base, (void (*)())ngx_mongo, 1, r);
		ngx_http_set_ctx(r, ctx, ngx_http_mongo_module);
	}

	r->write_event_handler = ngx_http_mongo;
	ngx_http_read_client_request_body(r, ngx_http_mongo);
	return NGX_DONE;
}

static char *ngx_http_mongo_parse(ngx_conf_t *cf, ngx_command_t *cmd, void *pconf)
{
	ngx_http_mongo_loc_t *conf = pconf;
	ngx_http_core_loc_conf_t *clcf;
	ngx_str_t *value = cf->args->elts;
	ngx_url_t u;

	ngx_memzero(&u, sizeof(u));
	u.no_resolve = 1;
	u.url = value[1];

	printf("found found found\n");

	conf->conf = ngx_http_upstream_add(cf, &u, 0);

	if (conf->conf == NULL)
	{
		printf("errort erroer error.\n");
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "no found url:%V", &value[1]);
		return NGX_CONF_ERROR;
	}

	clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

	if (clcf->name.data[clcf->name.len - 1] == '/')
		clcf->auto_redirect = 1;

	clcf->handler = ngx_http_mongo_block;
	return NGX_CONF_OK;
}

static void
ngx_rbtree_insert_mongo(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t  **p;

    for ( ;; ) {

        p = ((ngx_rbtree_key_int_t) (node->key - temp->key) < 0)
            ? &temp->left : &temp->right;

        if (*p == sentinel) {
            break;
        }

        temp = *p;
    }

    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}

static void *ngx_http_mongo_create_loc(ngx_conf_t *cf)
{
	ngx_http_mongo_loc_t *conf = ngx_pcalloc(cf->pool, sizeof(*conf));

	if (conf == NULL)
	{
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,"no memory.");
		return NULL;
	}

	ngx_rbtree_init(&conf->root, &conf->sen, ngx_rbtree_insert_mongo);
	ngx_queue_init(&conf->mongos);
	return conf;
}

static char *ngx_http_mongo_merge_loc(ngx_conf_t *cf, void *parent, void *child)
{

	return NGX_CONF_OK;
}


