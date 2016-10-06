#include <stic.h>

#include <stdio.h> /* FILE fclose() fopen() */

#include "../../src/cfg/config.h"
#include "../../src/ui/quickview.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/file_streams.h"
#include "../../src/utils/matchers.h"
#include "../../src/filetype.h"

static void check_only_one_line_displayed(void);

TEST(no_extra_line_with_extra_padding)
{
	cfg.extra_padding = 1;
	lwin.window_rows = 2;
	check_only_one_line_displayed();
}

TEST(no_extra_line_without_extra_padding)
{
	cfg.extra_padding = 0;
	lwin.window_rows = 0;
	check_only_one_line_displayed();
}

TEST(preview_can_match_agains_full_paths)
{
	char *error;
	matchers_t *ms;

	ft_init(NULL);

	assert_non_null(ms = matchers_alloc("{{*/*}}", 0, 1, "", &error));
	assert_null(error);

	ft_set_viewers(ms, "the-viewer");

	assert_string_equal("the-viewer",
			qv_get_viewer(TEST_DATA_PATH "/read/two-lines"));

	ft_reset(0);
}

TEST(preview_enabled_if_possible)
{
	assert_success(qv_ensure_is_shown());
	assert_success(qv_ensure_is_shown());

	curr_stats.view = 0;
	other_view->explore_mode = 1;
	assert_failure(qv_ensure_is_shown());
	other_view->explore_mode = 0;
}

TEST(when_preview_can_be_shown)
{
	assert_true(qv_can_show());

	other_view->explore_mode = 1;
	assert_false(qv_can_show());
	other_view->explore_mode = 0;
	assert_true(qv_can_show());

	curr_stats.number_of_windows = 1;
	assert_false(qv_can_show());
	curr_stats.number_of_windows = 2;
	assert_true(qv_can_show());
}

static void
check_only_one_line_displayed(void)
{
	char line[128];
	FILE *fp;

	fp = fopen(TEST_DATA_PATH "/read/two-lines", "r");

	other_view = &lwin;

	view_stream(fp, 0);

	line[0] = '\0';
	assert_non_null(get_line(fp, line, sizeof(line)));
	assert_string_equal("2nd line\n", line);

	fclose(fp);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
