#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include "kernel_apis.h"
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "output_infra.h"


STATE(tested);

/* Avoid parsing the same line/statement twice */
static int my_id;

/* Are we in an interesting API function (or a callee)? */
static int in_interesting_file = 0;
static int in_interesting_function = 0;
static struct kernel_api_func *current_api_func = NULL;
static GHashTable *previously_found_funcs = NULL;
static GHashTable *arg_states = NULL;


/* How is a variable used? */
struct usage_context {
	int atomic;
	char *atomic_expr;
};

/* The id of an API argument */
struct api_arg {
   struct kernel_api_func *api_func;
   int arg_id;
};



static void build_function_list(void) {
    struct kernel_api_func *k;
    previously_found_funcs = g_hash_table_new (g_str_hash, g_str_equal);
 
    for (size_t i = 0; i < sizeof(kernel_api_funcs) / sizeof(*kernel_api_funcs); i++) {
        k = &kernel_api_funcs[i];
        if (!g_hash_table_lookup(previously_found_funcs, k->api_func)) {
            g_hash_table_insert(previously_found_funcs, (void *)k->api_func, k);
        }
        in_interesting_file++;
    }
 }

static unsigned int pointer_hash(const void *ptr) {
   return (long)ptr;
}

static int pointer_eq(const void *ptr1, const void *ptr2) {
   return ptr1 == ptr2;
}

static void match_fundef(struct symbol *sym)
{
   if(previously_found_funcs == NULL)
      build_function_list();

   if(arg_states == NULL)
      arg_states = g_hash_table_new(&pointer_hash, &pointer_eq);

   struct kernel_api_func *k = g_hash_table_lookup(previously_found_funcs, get_function());
   if(k) {
      in_interesting_function++; 
      if(in_interesting_function > 1) {
         debug_std("Nested API function %s -> %s\n", current_api_func->api_func, get_function()); 
      }
      current_api_func = k;
      debug_std("API function %s (%s.%s) basefile %s\n", get_function(), current_api_func->api_name, current_api_func->api_field, get_base_file());
      {
         struct symbol *arg;
         int i = 0;

         FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
            if (!arg->ident) {
               continue;
            }

            // Two states are saved: one to check that the variable is of interest, another to
            // save the index of the argument
            struct smatch_state *arg_state = malloc(sizeof(*arg_state));
            arg_state->name = NULL;
            
            struct api_arg *aa = malloc(sizeof(*aa));
            aa->api_func = current_api_func;
            aa->arg_id = i;

            g_hash_table_insert(arg_states, arg_state, aa);

            set_state(my_id, arg->ident->name, arg, arg_state);

            i++;
         } END_FOR_EACH_PTR(arg);
      }
   }
   // callchain_add_fun(get_filename(), get_function(), my_id);
}


static void match_func_end(struct symbol *sym)
{
   struct kernel_api_func *k = g_hash_table_lookup(previously_found_funcs, get_function());
   if(k) {
      in_interesting_function--;
      assert(in_interesting_function >= 0);
      debug_std("END API function %s\n", get_function());
   }
   // callchain_rm_fun(get_function(), my_id);
}

static struct api_arg *get_arg_from_tag(struct expression *expr) {

	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm) {
      return NULL;
   }

   struct sm_state *tmp;
   struct api_arg *res;

	FOR_EACH_PTR(sm->possible, tmp) {

      res = g_hash_table_lookup(arg_states, tmp->state);
      if (res != 0)
         return res;
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

static void print_arg(struct api_arg *arg) {
   fprintf(out, "(%s.%s.%d) (%s.%s) %s:%d",
            arg->api_func->api_name,
            arg->api_func->api_field,
            arg->arg_id,
            arg->api_func->impl_name,
            arg->api_func->api_func,
            get_filename(),
            get_lineno()
   );
}

static void match_deref(struct expression *expr) {

   if (get_state_expr(my_id, expr) == &tested) {
      return;
   }

   struct api_arg *arg = get_arg_from_tag(expr);
   if (arg) {
      fprintf(out, "deref of ");
      print_arg(arg);
      fprintf(out, "\n");
   }
}


static void match_assign(struct expression *expr) {
    if (expr->type != EXPR_ASSIGNMENT)
      return;


    if (implied_not_equal(expr->right, 0)) {
      set_state_expr(my_id, expr->left, &tested);
      return;
    }

    struct sm_state *rstates = get_sm_state_expr(my_id, expr->right);
    struct sm_state *rstate;
    if (!rstates) {
      return;
    }

    struct sm_state *lstates = get_sm_state_expr(my_id, expr->right);
    if (!lstates) {
      while (!empty_ptr_list(&lstates->possible)) {
         pop_ptr_list(&lstates->possible);
      }
    }

    FOR_EACH_PTR(rstates->possible, rstate) {
      debug("\tassignment of tag %s\n", show_state(rstate->state));
      set_state_expr(my_id, expr->left, rstate->state);
    } END_FOR_EACH_PTR(rstate);
}

static void match_condition(struct expression *expr) {
  struct api_arg *arg = get_arg_from_tag(expr);
  if (arg) {
    if (!is_pointer(expr)) {
      return;
    }

    if (expr->type == EXPR_ASSIGNMENT) {
      match_condition(expr->right);
      match_condition(expr->left);
    }

    if (implied_not_equal(expr, 0))
      return;

    if (get_state_expr(my_id, expr)) {
      fprintf(out, "test of ");
      print_arg(arg);
      fprintf(out, "\n");

      if (expr->type == EXPR_SYMBOL) {
        set_true_false_states_expr(my_id, expr, &tested, NULL);
      }
    }

    return;
  }
}

void check_apis_tag_args(int id)
{
	if(!getenv("CHECK_DEREF"))
		return;

   init_output();

	my_id = id;
	add_hook(&match_fundef, FUNC_DEF_HOOK);
	add_hook(&match_func_end, FUNC_DEF_HOOK);
   add_dereference_hook(&match_deref);
   add_hook(&match_assign, ASSIGNMENT_HOOK);

   add_hook(&match_condition, CONDITION_HOOK);
}
