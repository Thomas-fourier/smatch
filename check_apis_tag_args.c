#include "smatch.h"
#include "smatch_slist.h"
#include "kernel_apis.h"
#include <glib.h>
#include <assert.h>


// Only track if a variable is of interest, and print 
STATE(api_arg);


//#define debug(...) fprintf(stderr, __VA_ARGS__)
#define debug(...)

/* Avoid parsing the same line/statement twice */
static int my_id;

/* Are we in an interesting API function (or a callee)? */
static int in_interesting_file = 0;
static int in_interesting_function = 0;
static struct kernel_api_func *current_api_func = NULL;
static GHashTable *previously_found_funcs = NULL;

/* How is a variable used? */
struct usage_context {
	int atomic;
	char *atomic_expr;
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


static void match_fundef(struct symbol *sym)
{
   if(previously_found_funcs == NULL)
      build_function_list();

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

         FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
            if (!arg->ident) {
               continue;
            }
            set_state(my_id, arg->ident->name, arg, &api_arg);
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
   } else {
      printf("END SUB function %s\n", get_function());
   }
   // callchain_rm_fun(get_function(), my_id);
}


static bool is_api_arg(struct expression *expr) {

	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &api_arg))
		return 1;
	return 0;
}

static void match_deref(struct expression *expr) {
   if (is_api_arg(expr)) {
      printf("deref of API in function %s, API name %s, field %s\n", get_function(), current_api_func->api_name, current_api_func->api_field);
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
