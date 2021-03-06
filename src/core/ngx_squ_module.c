
/*
 * Copyright (C) Ngwsx
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_md5.h>
#include <ngx_squ.h>


#define NGX_SQU_MAX_MODULES  64


typedef ngx_module_t **(*ngx_squ_get_modules_pt)(void);


static ngx_int_t ngx_squ_module_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_squ_master_init(ngx_log_t *log);
static void ngx_squ_master_exit(ngx_cycle_t *cycle);
static ngx_int_t ngx_squ_process_init(ngx_cycle_t *cycle);
static void ngx_squ_process_exit(ngx_cycle_t *cycle);
static ngx_int_t ngx_squ_thread_init(ngx_cycle_t *cycle);
static void ngx_squ_thread_exit(ngx_cycle_t *cycle);

static void *ngx_squ_create_conf(ngx_cycle_t *cycle);
static char *ngx_squ_init_conf(ngx_cycle_t *cycle, void *conf);
static char *ngx_squ_load_module(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_squ_set_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_squ_commands[] = {

    { ngx_string("squ_load_module"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_squ_load_module,
      0,
      0,
      NULL },

    { ngx_string("squ_set_directive"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_1MORE,
      ngx_squ_set_directive,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_squ_module_ctx = {
    ngx_string("squ"),
    ngx_squ_create_conf,
    ngx_squ_init_conf,
};


ngx_module_t  ngx_squ_module = {
    NGX_MODULE_V1,
    &ngx_squ_module_ctx,                   /* module context */
    ngx_squ_commands,                      /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    ngx_squ_master_init,                   /* init master */
    ngx_squ_module_init,                   /* init module */
    ngx_squ_process_init,                  /* init process */
    ngx_squ_thread_init,                   /* init thread */
    ngx_squ_thread_exit,                   /* exit thread */
    ngx_squ_process_exit,                  /* exit process */
    ngx_squ_master_exit,                   /* exit master */
    NGX_MODULE_V1_PADDING
};


/* The eight fixed arguments */

static ngx_uint_t  argument_number[] = {
    NGX_CONF_NOARGS,
    NGX_CONF_TAKE1,
    NGX_CONF_TAKE2,
    NGX_CONF_TAKE3,
    NGX_CONF_TAKE4,
    NGX_CONF_TAKE5,
    NGX_CONF_TAKE6,
    NGX_CONF_TAKE7
};


extern ngx_module_t  ngx_squ_cache_module;
extern ngx_module_t  ngx_squ_parser_module;
extern ngx_module_t  ngx_squ_autorun_module;
extern ngx_module_t  ngx_squ_dahua_module;
extern ngx_module_t  ngx_squ_dbd_module;
extern ngx_module_t  ngx_squ_dbd_libdrizzle_module;
extern ngx_module_t  ngx_squ_dbd_sqlite3_module;
extern ngx_module_t  ngx_squ_file_module;
extern ngx_module_t  ngx_squ_logger_module;
extern ngx_module_t  ngx_squ_smtp_module;
extern ngx_module_t  ngx_squ_socket_module;
extern ngx_module_t  ngx_squ_utils_module;
extern ngx_module_t  ngx_squ_webservice_module;
extern ngx_module_t  ngx_squ_xml_module;
extern ngx_module_t  ngx_squ_http_request_module;
extern ngx_module_t  ngx_squ_http_response_module;
extern ngx_module_t  ngx_squ_session_module;
extern ngx_module_t  ngx_squ_http_session_module;
extern ngx_module_t  ngx_squ_http_variable_module;
extern ngx_module_t  ngx_squ_http_btt_module;
extern ngx_module_t  ngx_squ_tcp_request_module;
extern ngx_module_t  ngx_squ_tcp_response_module;
extern ngx_module_t  ngx_squ_udp_request_module;
extern ngx_module_t  ngx_squ_udp_btt_module;
extern ngx_module_t  ngx_squ_udt_request_module;
extern ngx_module_t  ngx_squ_udt_response_module;


ngx_module_t  *ngx_squ_modules[NGX_SQU_MAX_MODULES] = {
    &ngx_squ_cache_module,
    &ngx_squ_parser_module,
#if !(NGX_SQU_DLL)
    &ngx_squ_autorun_module,
#if 0
    &ngx_squ_dahua_module,
#if (NGX_SQU_DBD)
    &ngx_squ_dbd_module,
#if (NGX_SQU_DBD_LIBDRIZZLE)
    &ngx_squ_dbd_libdrizzle_module,
#endif
#if (NGX_SQU_DBD_SQLITE3)
    &ngx_squ_dbd_sqlite3_module,
#endif
#endif
    &ngx_squ_file_module,
#endif
    &ngx_squ_logger_module,
#if 0
    &ngx_squ_smtp_module,
    &ngx_squ_socket_module,
#endif
    &ngx_squ_utils_module,
#if 0
    &ngx_squ_webservice_module,
    &ngx_squ_xml_module,
    &ngx_squ_http_request_module,
    &ngx_squ_http_response_module,
    /* TODO */
    &ngx_squ_session_module,
    &ngx_squ_http_session_module,
    &ngx_squ_http_variable_module,
    &ngx_squ_http_btt_module,
    &ngx_squ_tcp_request_module,
    &ngx_squ_tcp_response_module,
    &ngx_squ_udp_request_module,
    &ngx_squ_udp_btt_module,
#if (NGX_UDT)
    &ngx_squ_udt_request_module,
    &ngx_squ_udt_response_module,
#endif
#endif
#endif
    NULL
};


ngx_uint_t         ngx_squ_max_module;
static ngx_uint_t  ngx_squ_max_handle;


static ngx_int_t
ngx_squ_module_init(ngx_cycle_t *cycle)
{
    ngx_uint_t           m;
    ngx_module_t        *module;
    ngx_squ_conf_t      *scf;
    ngx_pool_cleanup_t  *cln;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ module init");

    scf = (ngx_squ_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_squ_module);

    if (ngx_squ_create(cycle, scf) == NGX_ERROR) {
        return NGX_ERROR;
    }

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->init_module == NULL) {
            continue;
        }

        if (module->init_module(cycle) == NGX_ERROR) {
            ngx_squ_destroy(scf);
            return NGX_ERROR;
        }
    }

    cln = ngx_pool_cleanup_add(cycle->pool, 0);
    if (cln == NULL) {
        ngx_squ_destroy(scf);
        return NGX_ERROR;
    }

    cln->handler = ngx_squ_destroy;
    cln->data = scf;

    return NGX_OK;
}


static ngx_int_t
ngx_squ_master_init(ngx_log_t *log)
{
    ngx_uint_t     m;
    ngx_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "squ master init");

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->init_master == NULL) {
            continue;
        }

        if (module->init_master(log) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_squ_master_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t     m;
    ngx_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ master exit");

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->exit_master != NULL) {
            module->exit_master(cycle);
        }
    }
}


static ngx_int_t
ngx_squ_process_init(ngx_cycle_t *cycle)
{
    ngx_uint_t     m;
    ngx_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ process init");

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->init_process == NULL) {
            continue;
        }

        if (module->init_process(cycle) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_squ_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t       m, h;
    ngx_module_t    *module;
    ngx_squ_conf_t  *scf;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ process exit");

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->exit_process != NULL) {
            module->exit_process(cycle);
        }
    }

    scf = (ngx_squ_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_squ_module);

    for (h = 0; h < ngx_squ_max_handle; h++) {
        if (scf->handle[h] != NULL) {
            ngx_squ_dlclose(scf->handle[h]);
        }
    }
}


static ngx_int_t
ngx_squ_thread_init(ngx_cycle_t *cycle)
{
    ngx_uint_t     m;
    ngx_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ thread init");

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->init_thread == NULL) {
            continue;
        }

        if (module->init_thread(cycle) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_squ_thread_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t     m;
    ngx_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ thread exit");

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        if (module->exit_thread != NULL) {
            module->exit_thread(cycle);
        }
    }
}


static void *
ngx_squ_create_conf(ngx_cycle_t *cycle)
{
    void               *rv;
    ngx_uint_t          m;
    ngx_squ_conf_t     *scf;
    ngx_core_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ create conf");

    scf = ngx_pcalloc(cycle->pool, sizeof(ngx_squ_conf_t));
    if (scf == NULL) {
        return NULL;
    }

    scf->handle = ngx_pcalloc(cycle->pool,
                              sizeof(void *) * NGX_SQU_MAX_MODULES);
    if (scf->handle == NULL) {
        return NULL;
    }

    scf->conf = ngx_pcalloc(cycle->pool, sizeof(void *) * NGX_SQU_MAX_MODULES);
    if (scf->conf == NULL) {
        return NULL;
    }

    ngx_squ_max_module = 0;
    for (m = 0; ngx_squ_modules[m] != NULL; m++) {
        ngx_squ_modules[m]->index = ngx_squ_max_module++;
    }

    for (m = 0; m < ngx_squ_max_module; m++) {
        if (ngx_squ_modules[m]->type != NGX_CORE_MODULE) {
            continue;
        }

        module = ngx_squ_modules[m]->ctx;

        if (module == NULL || module->create_conf == NULL) {
            continue;
        }

        rv = module->create_conf(cycle);
        if (rv == NULL) {
            return NULL;
        }

        scf->conf[ngx_squ_modules[m]->index] = rv;
    }

#if (NGX_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
#endif

    return scf;
}


static char *
ngx_squ_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_squ_conf_t *scf = conf;

    char               *rc;
    ngx_uint_t          m;
    ngx_core_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "squ init conf");

    for (m = 0; m < ngx_squ_max_module; m++) {
        if (ngx_squ_modules[m]->type != NGX_CORE_MODULE) {
            continue;
        }

        module = ngx_squ_modules[m]->ctx;

        if (module == NULL || module->init_conf == NULL) {
            continue;
        }

        rc = module->init_conf(cycle, scf->conf[ngx_squ_modules[m]->index]);
        if (rc != NGX_CONF_OK) {
            return rc;
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_squ_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_squ_conf_t *scf = conf;

    void                     *handle, *rv;
    ngx_str_t                *value;
    ngx_module_t            **mp, *m;
    ngx_core_module_t        *module;
    ngx_squ_get_modules_pt    get;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cf->log, 0, "squ load module");

    if (ngx_squ_max_module >= NGX_SQU_MAX_MODULES
        || ngx_squ_max_handle >= NGX_SQU_MAX_MODULES)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "not allowed more modules can be loaded");
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    if (ngx_conf_full_name(cf->cycle, &value[1], 0) == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    handle = ngx_squ_dlopen((char *) value[1].data);
    if (handle == NULL) {
        ngx_conf_log_error(NGX_LOG_ALERT, cf, ngx_errno,
                           ngx_squ_dlopen_n " \"%V\" failed (%s)",
                           &value[1], ngx_squ_dlerror());
        return NGX_CONF_ERROR;
    }

    get = (ngx_squ_get_modules_pt) ngx_squ_dlsym(handle, "ngx_squ_get_modules");
    if (get == NULL) {
        ngx_conf_log_error(NGX_LOG_ALERT, cf, ngx_errno,
                           ngx_squ_dlsym_n " \"ngx_squ_get_modules\" "
                           "in \"%V\" failed",
                           &value[1]);
        ngx_squ_dlclose(handle);
        return NGX_CONF_ERROR;
    }

    mp = get();
    if (mp == NULL) {
        goto done;
    }

    for (m = *mp; m != NULL; mp++, m = *mp) {

        if (ngx_squ_max_module >= NGX_SQU_MAX_MODULES) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "not allowed more modules can be loaded");
            return NGX_CONF_ERROR;
        }

        m->index = ngx_squ_max_module;

        if (m->type != NGX_CORE_MODULE) {
            goto next;
        }

        module = m->ctx;

        if (module == NULL || module->create_conf == NULL) {
            goto next;
        }

        rv = module->create_conf(cf->cycle);
        if (rv == NULL) {
            ngx_squ_dlclose(handle);
            return NGX_CONF_ERROR;
        }

        scf->conf[m->index] = rv;

next:

        ngx_squ_modules[ngx_squ_max_module++] = m;
    }

done:

    scf->handle[ngx_squ_max_handle++] = handle;

    return NGX_CONF_OK;
}


static char *
ngx_squ_set_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_squ_conf_t *scf = conf;

    char          *rv;
    ngx_str_t     *value, *name;
    ngx_uint_t     m, n, multi;
    ngx_module_t  *module;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cf->log, 0, "squ set directive");

    value = cf->args->elts;
    name = &value[1];
    multi = 0;

    for (m = 0; m < ngx_squ_max_module; m++) {
        module = ngx_squ_modules[m];

        if (module->type != NGX_CORE_MODULE) {
            continue;
        }

        cmd = module->commands;
        if (cmd == NULL) {
            continue;
        }

        for ( /* void */ ; cmd->name.len; cmd++) {

            if (name->len != cmd->name.len) {
                continue;
            }

            if (ngx_strcmp(name->data, cmd->name.data) != 0) {
                continue;
            }


            /* is the directive's location right ? */

            if (!(cmd->type & cf->cmd_type)) {
                if (cmd->type & NGX_CONF_MULTI) {
                    multi = 1;
                    continue;
                }

                goto not_allowed;
            }

            /* is the directive's argument count right ? */

            n = cf->args->nelts - 1;

            if (!(cmd->type & NGX_CONF_ANY)) {

                if (cmd->type & NGX_CONF_FLAG) {

                    if (n != 2) {
                        goto invalid;
                    }

                } else if (cmd->type & NGX_CONF_1MORE) {

                    if (n < 2) {
                        goto invalid;
                    }

                } else if (cmd->type & NGX_CONF_2MORE) {

                    if (n < 3) {
                        goto invalid;
                    }

                } else if (n > NGX_CONF_MAX_ARGS) {

                    goto invalid;

                } else if (!(cmd->type & argument_number[n - 1]))
                {
                    goto invalid;
                }
            }

            cf->args->elts = value + 1;
            cf->args->nelts = n;

            rv = cmd->set(cf, cmd, scf->conf[module->index]);

            cf->args->elts = value;
            cf->args->nelts = n + 1;

            if (rv == NGX_CONF_OK || rv == NGX_CONF_ERROR) {
                return rv;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"%s\" directive %s", name->data, rv);

            return NGX_CONF_ERROR;
        }
    }

    if (multi == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "unknown directive \"%s\"", name->data);
        return NGX_CONF_ERROR;
    }

not_allowed:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "\"%s\" directive is not allowed here", name->data);
    return NGX_CONF_ERROR;

invalid:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid number of arguments in \"%s\" directive",
                       name->data);

    return NGX_CONF_ERROR;
}


char *
ngx_squ_set_script_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    u_char            *name, result[16];
    ngx_str_t         *value, *path;
    ngx_md5_t          md5;
    ngx_squ_script_t  *script;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cf->log, 0, "squ set script slot");

    script = (ngx_squ_script_t *) (p + cmd->offset);

    if (script->from != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    name = cmd->name.data;

    if (ngx_strstr(name, "script_code") != NULL) {
        script->from = NGX_SQU_SCRIPT_FROM_CONF;
        script->code = value[1];

        ngx_md5_init(&md5);
        ngx_md5_update(&md5, script->code.data, script->code.len);
        ngx_md5_final(result, &md5);

        path = &script->path;

        path->data = ngx_pcalloc(cf->pool, 64);
        if (path->data == NULL) {
            return NGX_CONF_ERROR;
        }

        path->len = ngx_hex_dump(path->data, result, sizeof(result))
                    - path->data;

    } else {

        script->from = NGX_SQU_SCRIPT_FROM_FILE;
        script->path = value[1];

        if (ngx_conf_full_name(cf->cycle, &script->path, 0) == NGX_ERROR) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


char *
ngx_squ_set_script_parser_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    ngx_str_t         *value;
    ngx_squ_script_t  *script;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cf->log, 0,
                   "squ set script parser slot");

    script = (ngx_squ_script_t *) (p + cmd->offset);

    if (script->parser != NGX_CONF_UNSET_PTR) {
        return "is duplicate";
    }

    value = cf->args->elts;

    script->parser = ngx_squ_parser_find(cf->log, &value[1]);
    if (script->parser == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "the squ parser \"%V\" not found", &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
