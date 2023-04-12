#include <nxt_main.h>
#include <nxt_application.h>
#include <nxt_unit.h>
#include <nxt_unit_request.h>


#define SKEL_VERSION    "1.0"


static uint32_t  compat[] = {
    NXT_VERNUM, NXT_DEBUG,
};

static nxt_unit_ctx_t  *nxt_skel_unit_ctx;


static void
nxt_skel_request_handler(nxt_unit_request_info_t *req)
{
    printf("%s: \n", __func__);
}


static nxt_int_t
nxt_skel_setup(nxt_task_t *task, nxt_process_t *process,
    nxt_common_app_conf_t *conf)
{
    printf("%s: \n", __func__);

    return NXT_OK;
}


static nxt_int_t
nxt_skel_start(nxt_task_t *task, nxt_process_data_t *data)
{
    nxt_int_t              ret;
    nxt_unit_ctx_t         *unit_ctx;
    nxt_unit_init_t        skel_init;
    nxt_common_app_conf_t  *conf;

    printf("%s: \n", __func__);

    conf = data->app;

    ret = nxt_unit_default_init(task, &skel_init, conf);
    if (nxt_slow_path(ret != NXT_OK)) {
        nxt_alert(task, "nxt_unit_default_init() failed");
        return ret;
    }

    skel_init.callbacks.request_handler = nxt_skel_request_handler;

    unit_ctx = nxt_unit_init(&skel_init);
    if (nxt_slow_path(unit_ctx == NULL)) {
        return NXT_ERROR;
    }

    nxt_skel_unit_ctx = unit_ctx;

    nxt_unit_run(nxt_skel_unit_ctx);
    nxt_unit_done(nxt_skel_unit_ctx);

    exit(EXIT_SUCCESS);
}


NXT_EXPORT nxt_app_module_t  nxt_app_module = {
    sizeof(compat),
    compat,
    nxt_string("skel"),
    SKEL_VERSION,
    NULL,
    0,
    nxt_skel_setup,
    nxt_skel_start,
};
