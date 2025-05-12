#include "smatch.h"
#include "smatch_slist.h"
#include "kernel_apis.h"
#include <glib.h>
#include <assert.h>


//#define debug(...) fprintf(stderr, __VA_ARGS__)
#define debug(...)

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


static int is_same_driver(const char *api_file, const char *file) {
	// Do the files belong to the same directory?
	char *api_dir = g_path_get_dirname(api_file);
	char *dir = g_path_get_dirname(file);
	int ret = !strcmp(api_dir, dir);
	g_free(api_dir);
	g_free(dir);
	return ret;
}


static void build_function_list(void) {
    struct kernel_api_func *k;
    const char *base_file = get_base_file();
    previously_found_funcs = g_hash_table_new (g_str_hash, g_str_equal);
 
    for (size_t i = 0; i < sizeof(kernel_api_funcs) / sizeof(*kernel_api_funcs); i++) {
        k = &kernel_api_funcs[i];
        if (!is_same_driver(k->api_file, base_file))
            continue;
        // printf("Function %s added\n", k->api_func);
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
         printf("Nested API function %s -> %s\n", current_api_func->api_func, get_function()); 
      }
      current_api_func = k;
      printf("API function %s (%s.%s) basefile %s\n", get_function(), current_api_func->api_name, current_api_func->api_field, get_base_file());
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
      printf("END API function %s\n", get_function());
   }
   // callchain_rm_fun(get_function(), my_id);
}

static struct api_arg *get_arg_from_tag(struct expression *expr) {

	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm) {
      // fprintf(stderr, "Trying to get arg of possible non-arg\n");
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

static void match_deref(struct expression *expr) {

   struct api_arg *arg = get_arg_from_tag(expr);
   if (arg) {
      printf("deref of (%s.%s.%d) %s:%d\n", 
            arg->api_func->api_func,
            arg->api_func->api_field,
            arg->arg_id,
            get_filename(),
            get_lineno());
   }
}

void check_apis_tag_args(int id)
{
	if(!getenv("CHECK_DEREF"))
		return;

	my_id = id;
	add_hook(&match_fundef, FUNC_DEF_HOOK);
	add_hook(&match_func_end, FUNC_DEF_HOOK);
   add_dereference_hook(&match_deref);
}
