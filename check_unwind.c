/*
 * Copyright (C) 2020 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include <ctype.h>

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;
static int info_id;

STATE(alloc);
STATE(release);
STATE(param_released);
STATE(ignore);

static unsigned long fn_has_alloc;

struct ref_func_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static struct ref_func_info func_table[] = {
	{ "clk_prepare", ALLOC, 0, "$", &int_zero, &int_zero },
	{ "clk_prepare_enable", ALLOC, 0, "$", &int_zero, &int_zero },
	{ "clk_disable_unprepare", RELEASE, 0, "$" },
	{ "clk_unprepare", RELEASE, 0, "$" },

	{ "alloc_etherdev_mqs", ALLOC, -1, "$", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "free_netdev", RELEASE, 0, "$" },

	/*
	 * FIXME: A common pattern in release functions like amd76xrom_cleanup()
	 * is to do:
	 *
	 * 	if (window->rsrc.parent)
	 * 		release_resource(&window->rsrc);
	 *
	 * Which is slightly tricky to know how to merge the states so let's
	 * hold off checking request_resource() for now.
	 *
	 * { "request_resource", ALLOC,   1, "$", &int_zero, &int_zero },
	 * { "release_resource", RELEASE, 0, "$" },
	 *
	 */

	{ "pci_request_regions", ALLOC,   0, "$", &int_zero, &int_zero },
	{ "pci_release_regions", RELEASE, 0, "$" },

	{ "request_free_mem_region", ALLOC,   -1, "$->start", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "__request_region", ALLOC,   1, "$", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "release_and_free_resource", RELEASE, 0, "$->start" },
	{ "release_resource", RELEASE, 0, "$->start" },
	{ "__release_region", RELEASE, 1, "$" },

	{ "ioremap", ALLOC,  -1, "$", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "of_iomap", ALLOC,  -1, "$", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "ioremap_encrypted", ALLOC,  -1, "$", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "iounmap", RELEASE, 0, "$" },

	{ "request_threaded_irq", ALLOC,   0, "$", &int_zero, &int_zero },
	{ "request_irq", ALLOC,   0, "$", &int_zero, &int_zero },
	{ "free_irq",    RELEASE, 0, "$" },
	{ "pci_request_irq", ALLOC,   1, "$", &int_zero, &int_zero },
	{ "pci_free_irq",    RELEASE, 1, "$" },

	{ "register_netdev",   ALLOC,   0, "$", &int_zero, &int_zero },
	{ "unregister_netdev", RELEASE, 0, "$" },

	{ "misc_register",   ALLOC,   0, "$", &int_zero, &int_zero },
	{ "misc_deregister", RELEASE, 0, "$" },

	{ "ieee80211_alloc_hw", ALLOC,  -1, "$", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "ieee80211_free_hw",  RELEASE, 0, "$" },
};

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;

	if (sm->state != &param_released)
		return &undefined;

	if (is_impossible_path())
		return &param_released;

	state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (!state)
		return &undefined;
	if (!estate_rl(state) || is_err_or_null(estate_rl(state)))
		return &param_released;
	if (parent_is_err_or_null_var_sym(sm->name, sm->sym))
		return &param_released;

	if (estate_min(state).value == 0 &&
	    estate_max(state).value == 0)
		return &param_released;
	if (estate_type(state) == &int_ctype &&
	    sval_is_negative(estate_min(state)) &&
	    (estate_max(state).value == -1 || estate_max(state).value == 0))
		return &param_released;

	 return &undefined;
}

static bool is_param_var_sym(const char *name, struct symbol *sym)
{
	const char *key;

	return get_param_key_from_var_sym(name, sym, NULL, &key) >= 0;
}

static void mark_matches_as_undefined(const char *key)
{
	struct sm_state *sm;
	int start_pos, state_len, key_len;
	char *p;

	while ((p = strchr(key, '-'))) {
		if (p[1] != '>')
			return;
		key = p + 2;
	}
	key_len = strlen(key);

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		state_len = strlen(sm->name);
		if (state_len < key_len)
			continue;

		start_pos = state_len - key_len;
		if ((start_pos == 0 || !isalnum(sm->name[start_pos - 1])) &&
		    strcmp(sm->name + start_pos, key) == 0)
			update_ssa_state(my_id, sm->name, sm->sym, &undefined);

	} END_FOR_EACH_SM(sm);
}

static bool is_alloc_primitive(struct expression *expr)
{
	int i;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return false;

	if (expr->fn->type != EXPR_SYMBOL)
		return false;

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		if (sym_name_is(func_table[i].name, expr->fn))
			return true;
	}

	return false;
}

static void return_param_alloc(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct smatch_state *state;
	char *fn_name;

	fn_has_alloc = true;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;
	fn_name = expr_to_var(expr->fn);
	if (!fn_name)
		return;

	state = alloc_state_str(fn_name);
	state->data = &alloc;

	set_ssa_state(my_id, name, sym, state);
}

static void return_param_release(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct sm_state *start_sm;

	/* The !data means this comes from the DB (not hard coded). */
	if (!data && is_alloc_primitive(expr))
		return;

	start_sm = get_ssa_sm_state(my_id, name, sym);
	if (start_sm) {
		update_ssa_sm(my_id, start_sm, &release);
	} else {
		if (fn_has_alloc) {
			mark_matches_as_undefined(name);
			return;
		}
		if (is_param_var_sym(name, sym))
			set_state(info_id, name, sym, &param_released);
	}
}

static void ignore_path(const char *fn, struct expression *expr, void *data)
{
	set_state(my_id, "path", NULL, &ignore);
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct sm_state *sm;
	const char *param_name;
	int param;

	if (is_impossible_path())
		return;

	FOR_EACH_MY_SM(info_id, __get_cur_stree(), sm) {
		if (sm->state != &param_released)
			continue;
		param = get_param_key_from_sm(sm, expr, &param_name);
		if (param < 0)
			continue;
		sql_insert_return_states(return_id, return_ranges, RELEASE,
					 param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

enum {
	UNKNOWN, FAIL, SUCCESS, NUM_BUCKETS
};

static int success_fail_positive(struct range_list *rl)
{
	sval_t sval;

	if (!rl)
		return SUCCESS;

	// Negatives are a failure
	if (sval_is_negative(rl_min(rl)) && sval_is_negative(rl_max(rl)))
		return FAIL;

	// NULL and error pointers are a failure
	if (type_is_ptr(rl_type(rl)) && is_err_or_null(rl))
		return FAIL;

	if (rl_to_sval(rl, &sval)) {
		if (sval.value == 0) {
			// Zero is normally success but false is a failure
			if (type_bits(sval.type) == 1)
				return FAIL;
			else
				return SUCCESS;
		}
		// true is success
		if (sval.value == 1 && type_bits(sval.type) == 1)
			return SUCCESS;
	}

	return UNKNOWN;
}

static const char *get_alloc_fn(struct sm_state *sm)
{
	struct sm_state *tmp;
	const char *alloc_fn = NULL;
	bool released = false;

	if (sm->state->data == &alloc)
		return sm->state->name;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state->data == &alloc)
			alloc_fn = tmp->state->name;
		if (tmp->state == &release)
			released = true;
	} END_FOR_EACH_PTR(tmp);

	if (alloc_fn && !released)
		return alloc_fn;
	return NULL;
}

static void check_balance(const char *name, struct symbol *sym)
{
	struct range_list *inc_lines = NULL;
	int inc_buckets[NUM_BUCKETS] = {};
	struct stree *stree, *orig_stree;
	struct smatch_state *state;
	struct sm_state *return_sm;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	const char *fn_name = NULL;
	int bucket;

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig_stree = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;
		if (db_incomplete())
			goto swap_stree;
		if (get_state(my_id, "path", NULL) == &ignore)
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		sm = get_sm_state(my_id, name, sym);
		if (!sm)
			goto swap_stree;

		state = sm->state;
		if (state == &param_released)
			state = &release;

		fn_name = get_alloc_fn(sm);
		if (fn_name)
			state = &alloc;

		if (state != &alloc &&
		    state != &release)
			goto swap_stree;

		bucket = success_fail_positive(estate_rl(return_sm->state));
		if (bucket != FAIL)
			goto swap_stree;

		if (state == &alloc) {
			add_range(&inc_lines, line, line);
			inc_buckets[bucket] = true;
		}
swap_stree:
		__swap_cur_stree(orig_stree);
	} END_FOR_EACH_PTR(stree);

	if (inc_buckets[FAIL])
		goto complain;

	return;

complain:
	sm_warning("'%s' from %s() not released on lines: %s.", ssa_name(name), fn_name, show_rl(inc_lines));
}

static void match_check_balanced(struct symbol *sym)
{
	struct sm_state *sm;

	FOR_EACH_MY_SM(my_id, get_all_return_states(), sm) {
		if (sm->sym == NULL && strcmp(sm->name, "path") == 0)
			continue;
		check_balance(sm->name, sm->sym);
	} END_FOR_EACH_SM(sm);
}

void check_unwind(int id)
{
	struct ref_func_info *info;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	set_dynamic_states(my_id);

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];

		if (info->call_back) {
			add_function_hook(info->name, info->call_back, info);
		} else if (info->implies_start && info->type == ALLOC) {
			return_implies_param_key_exact(info->name,
					*info->implies_start,
					*info->implies_end,
					&return_param_alloc,
					info->param, info->key, info);
		} else if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start,
					*info->implies_end,
					&return_param_release,
					info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name,
				(info->type == ALLOC) ? &return_param_alloc : &return_param_release,
				info->param, info->key, info);
		}
	}

	add_function_hook("devm_add_action_or_reset", &ignore_path, NULL);
	add_function_hook("drmm_add_action", &ignore_path, NULL);
	add_function_hook("__drmm_add_action", &ignore_path, NULL);
	add_function_hook("pcim_enable_device", &ignore_path, NULL);
	add_function_hook("pci_enable_device", &ignore_path, NULL);

	add_function_data(&fn_has_alloc);

	add_split_return_callback(match_return_info);
	select_return_param_key(RELEASE, &return_param_release);
	add_hook(&match_check_balanced, END_FUNC_HOOK);
}

void check_unwind_info(int id)
{
	info_id = id;
	add_unmatched_state_hook(info_id, &unmatched_state);
}
