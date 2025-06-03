#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include "kernel_apis.h"
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "output_infra.h"


STATE(tested);
STATE(unknown);

#define MAX_FIELD_LENGTH 64

/* Avoid parsing the same line/statement twice */
static int my_id;

/* Are we in an interesting API function (or a callee)? */
static int in_interesting_file = 0;
static int in_interesting_function = 0;
static struct kernel_api_func *current_api_func = NULL;
static GHashTable *previously_found_funcs = NULL;
static GHashTable *state_to_arg = NULL;


/* How is a variable used? */
struct usage_context {
	int atomic;
	char *atomic_expr;
};

/* The id of an API argument */
struct api_arg {
   struct kernel_api_func *api_func;
   char field_id[MAX_FIELD_LENGTH];
   bool is_owned; // Is it owned by state_to_arg?
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

   if(state_to_arg == NULL)
      state_to_arg = g_hash_table_new(&pointer_hash, &pointer_eq);

   struct kernel_api_func *k = g_hash_table_lookup(previously_found_funcs, get_function());
   if(k) {
      in_interesting_function++;
      if(in_interesting_function > 1) {
         debug_std("Nested API function %s -> %s\n", current_api_func->api_func, get_function()); 
      }
      debug_std("API function %s (%s.%s) basefile %s\n", get_function(), k->api_name, k->api_field, get_base_file());
   }


   current_api_func = k;

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
      aa->is_owned = true;
      snprintf(aa->field_id, MAX_FIELD_LENGTH, "%d", i);

      g_hash_table_insert(state_to_arg, arg_state, aa);

      set_state(my_id, arg->ident->name, arg, arg_state);

      i++;
   } END_FOR_EACH_PTR(arg);

}


static void match_func_end(struct symbol *sym)
{
   struct kernel_api_func *k = g_hash_table_lookup(previously_found_funcs, get_function());
   if(k) {
      in_interesting_function--;
      assert(in_interesting_function >= 0);
      debug_std("END API function %s\n", get_function());
   }
   if (current_api_func) {
      // TODO: test if current api derefs without testing and if so look
      // at other implementations and warn if necessary
      current_api_func = NULL;
   }
}

static struct api_arg *get_arg_from_tag(struct expression *expr) {
	struct sm_state *sm;
   struct sm_state *tmp;
   struct api_arg *res;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm) {
      return NULL;
   }


	FOR_EACH_PTR(sm->possible, tmp) {
      res = g_hash_table_lookup(state_to_arg, tmp->state);
      if (res != 0)
         return res;
	} END_FOR_EACH_PTR(tmp);

	return NULL;
}

static struct api_arg *append_sub_field(struct api_arg *sub_arg, char *field) {
   struct api_arg *arg;

   // If it is a state, it is owned by `state_to_arg`, copy, otherwise modify in place
   if (sub_arg->is_owned) {
      arg = malloc(sizeof(*arg));
      memcpy(arg, sub_arg, sizeof(*arg));
      arg->is_owned = false;
   } else {
      arg = sub_arg;
   }
   if (strlen(arg->field_id) + strlen(field) + 2 > MAX_FIELD_LENGTH) {
      sm_warning("Field name too long: %s.%s", arg->field_id, field);
   }

   // Append the subfield and return
   strncat(arg->field_id, ".", MAX_FIELD_LENGTH - strlen(arg->field_id) - 1);
   strncat(arg->field_id, field, MAX_FIELD_LENGTH - strlen(arg->field_id) - 1);

   return arg;
}

static struct api_arg *member_of_arg(struct expression *expr) {
   struct api_arg *arg;
   struct api_arg *sub_arg;

   if ((expr->type == EXPR_PREOP && expr->op == '*')) {
      expr = expr->deref;
   }

   // Test if it is an arg already known
   if ((arg = get_arg_from_tag(expr))) {
      return arg;
   }

   // Test if if it is a field access of an arg
   if (expr->type == EXPR_DEREF && expr->member) {
      if ((sub_arg = member_of_arg(expr->deref))) {
         arg = append_sub_field(sub_arg, expr->member->name);
         return arg;
      }
   }

   return NULL;
}

static void print_arg(const char *msg,struct api_arg *arg) {
   if (!arg) return;
   if (arg->api_func) {
      fprintf(out, "%s (%s.%s.%s) (%s.%s) %s:%d\n",
               msg,
               arg->api_func->api_name,
               arg->api_func->api_field,
               arg->field_id,
               arg->api_func->impl_name,
               arg->api_func->api_func,
               get_filename(),
               get_lineno()
      );
   } else {
      fprintf(out, "%s (%s.%s) %s:%d\n",
               msg,
               get_function(),
               arg->field_id,
               get_filename(),
               get_lineno()
      );
   }
}

static void match_deref(struct expression *expr) {

   if (get_state_expr(my_id, expr) == &tested) {
      return;
   }

   struct api_arg *arg = member_of_arg(expr);
   if (arg) {
      print_arg("deref of", arg);

      // Save arg in database
      sql_insert_deref_fn_args(
              arg->field_id, arg->api_func ? arg->api_func->api_name : "",
              get_filename(), get_lineno());

      if (!arg->is_owned)
         free(arg);
   }
}


static int read_deref_line(void* expr, int nb_fields, char **values, char **fields) {
   struct expression *_expr = expr;
   int arg_id;
   char field[MAX_FIELD_LENGTH] = "\0";
   struct api_arg *arg;

   sscanf(values[0], "%d.%s", &arg_id, field);

   if ((arg = member_of_arg(get_argument_from_call_expr(_expr->args, arg_id)))) {
      // If the field is empty, do not add a dot
      if (field[0] != '\0') {
         arg = append_sub_field(arg, field);
      }

      print_arg("deref of", arg);

      // Save arg in database
      sql_insert_deref_fn_args(
              arg->field_id, arg->api_func ? arg->api_func->api_name : "",
              get_filename(), get_lineno());

   }
   
   return 0;
}

static void match_call(struct expression *expr) {

   if (expr->type != EXPR_CALL || !expr->fn) {
      return;
   }

   mem_sql(read_deref_line, expr,
           "SELECT param_num FROM deref_fn_args "
           "WHERE fn_name = '%s'",
           expr_to_str(expr->fn)
         );
}

static void clear_state(struct expression *expr) {
  set_state_expr(my_id, expr, &unknown);
  struct sm_state *states = get_sm_state_expr(my_id, expr->left);
  if (states) {
    while (!empty_ptr_list((states->possible))) {
      pop_ptr_list(&states->possible);
    }
  }
}

static void match_assign(struct expression *expr) {
   struct api_arg *arg;

   if (is_fake_var_assign(expr))
      return;

  if (expr->type != EXPR_ASSIGNMENT)
    return;

  // If it is a value substitution, nullity is not changed
  if ((expr->left->type == EXPR_PREOP && expr->left->op == '*')) {
    return;
  }


  // The state of the left is overwritten
  clear_state(expr->left);

  if ((arg = member_of_arg(expr->right))) {
      struct smatch_state *arg_state = malloc(sizeof(*arg_state));
      arg_state->name = NULL;

      // The arg is now owned by the hashtable
      arg->is_owned = true;
      g_hash_table_insert(state_to_arg, arg_state, arg);
      set_state_expr(my_id, expr->left, arg_state);
  }

  // If it is for sure a valid pointer, mark as tested
  if (implied_not_equal(expr->right, 0)
      || get_state_expr(my_id, expr->right) == &tested) {
    set_state_expr(my_id, expr->left, &tested);
  }
}

static void match_condition(struct expression *expr) {
  struct api_arg *arg = member_of_arg(expr);
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
      print_arg("test of", arg);

      // test if it is derefed and issue a warning
      // add to tested_pointer table

      if (expr->type == EXPR_SYMBOL) {
        set_true_false_states_expr(my_id, expr, &tested, NULL);
      }
    }

    if (!arg->is_owned) {
      free(arg);
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
	add_hook(&match_func_end, END_FUNC_HOOK);
   add_dereference_hook(&match_deref);
   add_hook(&match_assign, ASSIGNMENT_HOOK);
   add_hook(&match_call, FUNCTION_CALL_HOOK_BEFORE);
   add_hook(&match_condition, CONDITION_HOOK);
}
