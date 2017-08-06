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

typedef struct
{
	ngx_queue_t mongos;
	ngx_queue_t des_mongos;
	ngx_http_upstream_srv_conf_t *conf;
	ngx_pool_t *pool;
} ngx_http_mongo_loc_t;

typedef struct
{
	mongo db;
	ngx_queue_t data;
	ucontext_t base;
	ucontext_t db_base;
	u_char buff[MAX_STACK_LEN];
	ngx_connection_t *c;
	ngx_http_request_t *r;
	ngx_http_mongo_loc_t *conf;
	ngx_int_t ret;
} ngx_mongo_ctx_t;

static char *ngx_http_mongo_parse(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *ngx_http_mongo_create_loc(ngx_conf_t *cf);
static char *ngx_http_mongo_merge_loc(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t ngx_http_mongo_block(ngx_http_request_t *r);

static void ngx_http_mongo_destroy(ngx_mongo_ctx_t *p);

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
	ngx_mongo_ctx_t *ctx = c->data;
	ngx_http_request_t *r = ctx->r;


	if (r)
		r->write_event_handler(r);
	else
	{//闲杂没有要驱动的事情了
		u_char buf[1];
		ngx_int_t ret = recv(c->fd, buf, 1, MSG_PEEK);

		if (ret <= 0)
		{
			if (errno == EAGAIN || errno == EINTR)
				return;
			ngx_http_mongo_destroy(ctx);
		}
	}
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
		ctx->c = ngx_get_connection(pmongo->sock, ctx->r->connection->log);
		ctx->c->write->handler = ngx_mongo_event_parse;
		ctx->c->read->handler = ngx_mongo_event_parse;
		ctx->c->data = ctx;
		ngx_add_conn(ctx->c);
		swapcontext(&ctx->db_base, &ctx->base);
	}
	return 0;
}

int close_func(mongo *pmongo)
{
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
		ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));
	}
	else
	{
		ngx_http_finalize_request(r, NGX_ERROR);
	}

	return ;
}


static ngx_int_t ngx_parse_mongo(ngx_http_request_t *r, mongo *db)
{
	bson b;
	ngx_int_t count = 0;
	mongo_cursor *cursor;

	cursor = mongo_find(db, "test.test", bson_empty(&b), NULL, 0, 0, 0);

	while (cursor && mongo_cursor_next(cursor) == NGX_OK)
	{
		count++;
	}

	printf("count = %d\n", (int)count);

	mongo_cursor_destroy(cursor);
	return NGX_OK;
}

static void ngx_mongo(ngx_http_request_t *r)
{
	ngx_mongo_ctx_t *ctx;
	ngx_http_mongo_loc_t *conf;

	ctx = ngx_http_get_module_ctx(r, ngx_http_mongo_module);
	if (ctx->c == NULL)
	{
		char ip[129];
		short port;

		mongo_init(&ctx->db);

		conf = ngx_http_get_module_loc_conf(r, ngx_http_mongo_module);

		ngx_http_upstream_rr_peers_t *peers = conf->conf->peer.data;
		ngx_http_upstream_rr_peer_t *peer = &peers->peer[ngx_random() % peers->number];
		struct sockaddr_in *sin = (struct sockaddr_in *)peer->sockaddr;
		inet_ntop(AF_INET, &sin->sin_addr, ip, 128);
		port = ntohs(sin->sin_port);

		printf("ip = %s, port = %d\n", ip, port);
		if (mongo_connect(&ctx->db, ip, port) != MONGO_OK)
		{
			ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "connect error.");
			ctx->r = NULL;
			ctx->ret = NGX_ERROR;
			return;
		}
	}

	if (ngx_parse_mongo(r, &ctx->db) != NGX_OK)
	{
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "parse mongo.");
		ctx->r = NULL;
		ctx->ret = NGX_ERROR;
		return;
	}

	ctx->r = NULL;
	ctx->ret = NGX_OK;
	ngx_queue_insert_tail(&ctx->conf->mongos, &ctx->data);
}

static void ngx_http_mongo_destroy(ngx_mongo_ctx_t *ctx)
{
	ngx_close_connection(ctx->c);
	ctx->c = NULL;
	ctx->db.sock = -1;
	mongo_destroy(&ctx->db);
	ngx_queue_insert_tail(&ctx->conf->des_mongos, &ctx->data);
}

static void ngx_http_mongo_close(void *p)
{
	ngx_mongo_ctx_t *ctx = p;
	if (ctx->r)
	{
		ngx_http_mongo_destroy(ctx);
	}
}

static ngx_mongo_ctx_t *ngx_http_get_mongo_ctx(ngx_http_mongo_loc_t *clcf)
{
	ngx_mongo_ctx_t *ctx;

	if (clcf->mongos.next != &clcf->mongos)
	{
		ngx_queue_t *data = clcf->mongos.next;
		ngx_queue_remove(data);
		ctx = ngx_queue_data(data, ngx_mongo_ctx_t, data);
		ngx_queue_init(&ctx->data);
	}
	else if (clcf->des_mongos.next != &clcf->des_mongos)
	{
		ngx_queue_t *data = clcf->des_mongos.next;
		ngx_queue_remove(data);
		ctx = ngx_queue_data(data, ngx_mongo_ctx_t, data);
		ngx_queue_init(&ctx->data);
		ctx->c = NULL;
	}
	else
	{
		ctx = ngx_palloc(clcf->pool, sizeof(*ctx));

		if (ctx != NULL)
		{
			ngx_queue_init(&ctx->data);
			ctx->c = NULL;
			ctx->r = NULL;
			ctx->conf = clcf;
		}
	}

	return ctx;
}

static ngx_int_t ngx_http_mongo_block(ngx_http_request_t *r)
{
	ngx_pool_cleanup_t *cln;
	ngx_mongo_ctx_t *ctx;
	ngx_http_mongo_loc_t *conf;

	if (ngx_http_set_content_type(r) != NGX_OK)
	{
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"set content type error.");
		return NGX_ERROR;
	}

	ctx = ngx_http_get_module_ctx(r, ngx_http_mongo_module);

	if (ctx == NULL)
	{
		conf = ngx_http_get_module_loc_conf(r, ngx_http_mongo_module);
		cln = ngx_pool_cleanup_add(r->pool, 0);

		if (cln == NULL)
		{
			ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "no memory.");
			return NGX_ERROR;
		}

		ctx = ngx_http_get_mongo_ctx(conf);

		if(ctx == NULL)
		{
			ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "no memory.");
			return NGX_ERROR;
		}

		ctx->r = r;
		cln->data = ctx;
		cln->handler = ngx_http_mongo_close;

		getcontext(&ctx->base);
		getcontext(&ctx->db_base);

		ctx->db_base.uc_link = &ctx->base;
		ctx->db_base.uc_stack.ss_size = sizeof(ctx->buff);
		ctx->db_base.uc_stack.ss_sp = ctx->buff;

		ctx->ret = NGX_AGAIN;

		makecontext(&ctx->db_base, (void (*)())ngx_mongo, 1, r);
		ngx_http_set_ctx(r, ctx, ngx_http_mongo_module);
		r->write_event_handler = ngx_http_mongo;
	}

	ngx_http_read_client_request_body(r, ngx_http_mongo);

	return NGX_DONE;
}

static void ngx_http_mongo_destroy_pool(void *pconf)
{
	ngx_http_mongo_loc_t *conf = pconf;
	ngx_queue_t *p = &conf->mongos;
	ngx_queue_t *next;
	ngx_mongo_ctx_t *ctx;

	while (p != &conf->mongos)
	{
		next = p;
		p = ngx_queue_next(p);

		ctx = ngx_queue_data(next, ngx_mongo_ctx_t, data);

		ngx_http_mongo_destroy(ctx);
	}

	ngx_destroy_pool(conf->pool);
}

static char *ngx_http_mongo_parse(ngx_conf_t *cf, ngx_command_t *cmd, void *pconf)
{
	ngx_pool_cleanup_t *cln;
	ngx_http_mongo_loc_t *conf = pconf;
	ngx_http_core_loc_conf_t *clcf;
	ngx_str_t *value = cf->args->elts;
	ngx_url_t u;

	ngx_memzero(&u, sizeof(u));
	u.no_resolve = 1;
	u.url = value[1];

	conf->conf = ngx_http_upstream_add(cf, &u, 0);

	if (conf->conf == NULL)
	{
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "no found url:%V", &value[1]);
		return NGX_CONF_ERROR;
	}

	clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

	if (clcf->name.data[clcf->name.len - 1] == '/')
		clcf->auto_redirect = 1;

	if (conf->pool == NULL)
	{
		conf->pool = ngx_create_pool(ngx_pagesize, cf->log);

		if (conf->pool == NULL)
		{
			ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "no memory.");
			return NGX_CONF_ERROR;
		}

		cln = ngx_pool_cleanup_add(cf->pool, 0);

		if (cln == NULL)
		{
			ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "no memory.");
			return NGX_CONF_ERROR;
		}

		cln->handler = ngx_http_mongo_destroy_pool;
		cln->data = conf;
	}

	clcf->handler = ngx_http_mongo_block;
	return NGX_CONF_OK;
}

static void *ngx_http_mongo_create_loc(ngx_conf_t *cf)
{
	ngx_http_mongo_loc_t *conf = ngx_pcalloc(cf->pool, sizeof(*conf));

	if (conf == NULL)
	{
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,"no memory.");
		return NULL;
	}

	ngx_queue_init(&conf->mongos);
	ngx_queue_init(&conf->des_mongos);
	return conf;
}

static char *ngx_http_mongo_merge_loc(ngx_conf_t *cf, void *parent, void *child)
{
	return NGX_CONF_OK;
}


