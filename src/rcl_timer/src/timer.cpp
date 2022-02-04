#include <rcl/init.h>
#include <rcl/time.h>
#include <rcl/timer.h>
#include <rcl/wait.h>

#define OFFSET_MS 1000            // 1 second
#define WAIT_SET_TIMEOUT_MS 5000  // 5 seconds

// Test to check if rcl_wait returns without non-null timer immediately when timer is reset.
// It seems to be a bug from https://github.com/ros2/rcl/pull/589
#define TEST_TIMER_RESET 0

static void timer_cb(rcl_timer_t *timer, int64_t last_call_time) 
{
    RCL_UNUSED(timer);
    printf("timer callback is invoked. last_call_time=%lu\n", last_call_time);
}

int main(int argc, char **argv)
{
    int i;
    rcl_ret_t   ret;
    rcl_clock_t *clock = NULL;
    rcl_timer_t *timer = NULL;
    rcl_wait_set_t *wait_set = NULL;

    bool exit_cond = false;
    bool has_nonnull_timer = false;

    // create context
    rcl_context_t context = rcl_get_zero_initialized_context();

    // create init options
    rcl_init_options_t init_opts = rcl_get_zero_initialized_init_options();
    ret = rcl_init_options_init(&init_opts, rcl_get_default_allocator());
    if (ret != RCL_RET_OK) {
        printf("failed to initialize init options. ret=%d\n", ret);
        return ret;
    }

    // init rcl
    ret = rcl_init(argc, argv, &init_opts, &context);
    if (ret != RCL_RET_OK) {
        printf("failed to initialize rcl. ret=%d\n", ret);
        return ret;
    }

    // default allocator
    rcl_allocator_t allocator = rcl_get_default_allocator();

    // int clock
    clock = new rcl_clock_t();
    ret = rcl_clock_init(RCL_STEADY_TIME, clock, &allocator);
    if (ret != RCL_RET_OK) {
        printf("failed to initialize clock. ret=%d\n", ret);
        goto _error;
    }

    // init timer
    timer = new rcl_timer_t();
    *timer = rcl_get_zero_initialized_timer();
    ret = rcl_timer_init(timer, clock, &context, RCL_MS_TO_NS(OFFSET_MS), timer_cb, allocator);
    if (ret != RCL_RET_OK) {
        printf("failed to initilize timer, ret=%d\n", ret);
        goto _error;
    }

    // nit wait_set
    wait_set = new rcl_wait_set_t();
    *wait_set = rcl_get_zero_initialized_wait_set();
    ret = rcl_wait_set_init(wait_set, 0, 0, 1, 0, 0, 0, &context, allocator);
    if (ret != RCL_RET_OK) {
        printf("failed to initilize wait set, ret=%d\n", ret);
        goto _error;
    }

    // wait lopp
    do {
        ret = rcl_wait_set_clear(wait_set);
        if (ret != RCL_RET_OK) {
            printf("failed to clear wait set, ret=%d\n", ret);
            goto _error;
        }

        size_t index;
        ret = rcl_wait_set_add_timer(wait_set, timer, &index);
        if (ret != RCL_RET_OK) {
            printf("failed to add timer to wait set, ret=%d\n", ret);
            goto _error;
        }

#if TEST_TIMER_RESET
        bool canceled = false;
        ret = rcl_timer_is_canceled(timer, &canceled);
        if (ret != RCL_RET_OK) {
            printf("failed to check if timer is canceled, ret=%d\n", ret);
            goto _error;
        }

        if (canceled) {
            ret = rcl_timer_reset(timer);
            if (ret != RCL_RET_OK) {
                printf("failed to reset timer, ret=%d\n", ret);
                goto _error;
            }
        }
#endif

        has_nonnull_timer = false;

        ret = rcl_wait(wait_set, RCL_MS_TO_NS(WAIT_SET_TIMEOUT_MS));
        if (ret == RCL_RET_TIMEOUT) {
            printf("no awake timer during %d ms\n", WAIT_SET_TIMEOUT_MS);
            continue;
        } else if (ret != RCL_RET_OK) {
            printf("failed to wait, ret=%d\n", ret);
            goto _error;
        }

        for (i=0; i<(int) wait_set->size_of_timers; i++) {
            if (wait_set->timers[i]) {
                ret = rcl_timer_call(timer);
                if (ret != RCL_RET_OK) {
                    printf("failed to call timer callback , ret=%d\n", ret);
                    goto _error;
                }

#if TEST_TIMER_RESET
                ret = rcl_timer_cancel(timer);
                if (ret != RCL_RET_OK) {
                    printf("failed to cancel timer, ret=%d\n", ret);
                    goto _error;
                }
#endif

                has_nonnull_timer = true;
            }
        }

        if (!has_nonnull_timer) {
            printf("!!!!! rcl_wait was returned without non-null timer !!!!!!\n");
        }

    } while (!exit_cond);

_error:
    if (clock) {
        ret = rcl_clock_fini(clock);
        if (ret != RCL_RET_OK) {
            printf("Failed to finalize a clock, ret=%d\n", ret);
        }
        delete clock;
    }


    if (timer) {
        ret = rcl_timer_fini(timer);
        if (ret != RCL_RET_OK) {
            printf("Failed to finalize a timer, ret=%d\n", ret);
        }
        delete timer;
    }

    if (wait_set) {
        ret = rcl_wait_set_fini(wait_set);
        if (ret != RCL_RET_OK) {
            printf("Failed to finalize a wait set, ret=%d\n", ret);
        }
        delete wait_set;
    }

    ret = rcl_shutdown(&context);
    if (ret != RCL_RET_OK) {
        printf("Failed to shutdown rcl context., ret=%d\n", ret);
    }

    return ret;
}