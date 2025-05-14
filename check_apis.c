#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include <glib.h>
#include <stdio.h>
#include "output_infra.c"

/*
Find all kernel APIs.

We define a kernel API as a struct that only contains function pointers.

This script needs to be executed *before* check_apis_flow.c.

Usage:
   From Linux directory:
   $ ./smatch_scripts/test_kernel.sh | tee log_apis
   From this directory:
   $ ./parse_apis.pl <linux dir>/log_apis > kernel_apis.h
   $ make clean; make -j;

*/

static GHashTable *found_apis;
static GHashTable *found_global_assignments;

struct field_assignement {
   char *field;
   char *value;
   struct field_assignement *next;
};

/*
* Heuristic: a struct that only contains function pointers may be a kernel API.
* If multiple structures of that type are found throughout the kernel, then it is almost for sure an API.
*/
static bool is_maybe_api(struct symbol *sym)
{
	struct symbol *struct_type, *tmp, *base_type;
   if (sym->type == SYM_NODE)
      struct_type = get_real_base_type(sym);
   else
      struct_type = sym;

   // Either the struct is named ..._ops
   if(struct_type->ident) {
      char *struct_name = struct_type->ident->name;
      if(strlen(struct_name) < 4)
         return false;
      if(!strcmp(struct_name + strlen(struct_name) - 4, "_ops"))
         return true;
   }

   // ... or it only contains many function pointers
   int nb_function_pointers = 0;
	FOR_EACH_PTR(struct_type->symbol_list, tmp) {
		if (!tmp->ident)
			continue;
		base_type = get_real_base_type(tmp);
      if(base_type->type != SYM_PTR) // pointer...
         continue;
      base_type = get_real_base_type(base_type);
      if(base_type->type != SYM_FN) // ... to a function
         continue;
      nb_function_pointers++;
	} END_FOR_EACH_PTR(tmp);
   return nb_function_pointers > 3; // completely arbitrary
}

/*
* Print all members of a struct.
* /!\ Only works on symbols for which is_maybe_api() returned true.
*/
static __attribute__((unused)) void print_api_members(struct symbol *sym)
{
	struct symbol *struct_type, *tmp, *base_type;
   if (sym->type == SYM_NODE)
      struct_type = get_real_base_type(sym);
   else
      struct_type = sym;
	FOR_EACH_PTR(struct_type->symbol_list, tmp) {
		if (!tmp->ident)
			continue;
		base_type = get_real_base_type(tmp); // pointer...
      if(base_type->type != SYM_PTR) // pointer...
         continue;
      base_type = get_real_base_type(base_type); // ... to a function
      if(base_type->type != SYM_FN) // some ..._ops also have pointers to structs (payload)
         continue;
      debug("\t%s.%s = %s is a function pointer (%s)\n", sym->ident->name, tmp->ident->name, "", type_to_str(base_type));
	} END_FOR_EACH_PTR(tmp);
	FOR_EACH_PTR(struct_type->arguments, tmp) {
      debug("Something!!\n");
	} END_FOR_EACH_PTR(tmp);
}

static void print_found_assignements(char *api) {
   struct field_assignement *f = g_hash_table_lookup(found_global_assignments, api);
   while(f) {
      fprintf(out, "API - %s.%s = %s\n", api, f->field, f->value);
      f = f->next;
   }
}

static bool match_sym(struct expression *expr)
{
   //printf("Expression %d: %s\n", expr->type, expr_to_str(expr));
   struct symbol *sym = expr_to_sym(expr);
   if(!sym)
      return false;
   if(toplevel(sym->scope)) {
      if(is_struct_type(sym)) {
         // Structs are sometimes encapsulated in a node symbol
         if (sym->type == SYM_NODE)
            sym = get_real_base_type(sym);
         if(is_maybe_api(sym)) {
            char *api = g_strdup(type_to_str(sym));
            // Don't print an API twice
            if(g_hash_table_lookup(found_apis, api))
               return true;
            g_hash_table_insert(found_apis, api, &sym);

            fprintf(out, "API '%s' '%s' decl %d to %d file %s\n", api, expr_to_var(expr), sym->pos.line, sym->endpos.line, get_filename());
            //print_api_members(sym);
            print_found_assignements(expr_to_var(expr));
            return true;
         }
      }
   }
   return false;
}

static void match_assign(struct expression *expr) {
   // [base_struct_name].[field] = [value]
   char *base_struct_name = NULL, *field = NULL, *value = NULL;
   struct symbol *base_struct = expr_to_sym(expr->left);
   struct symbol *assignement = expr_to_sym(expr->right);
   if(base_struct && base_struct->ident)
      base_struct_name = g_strdup(base_struct->ident->name);
   if(expr->left->type == EXPR_DEREF && expr->left->member)
      field = expr->left->member->name;
   if(assignement && assignement->ident)
      value = assignement->ident->name;

   if(!base_struct_name || !field || !value)
      return;

   struct field_assignement *old = g_hash_table_lookup(found_global_assignments, base_struct_name);
   struct field_assignement *new = malloc(sizeof(*new));
   new->field = strdup(field);
   new->value = strdup(value);
   new->next = old;
   g_hash_table_insert(found_global_assignments, base_struct_name, new);
}

void check_apis(int id)
{
	if(!getenv("CHECK_APIS"))
		return;
   

   init_output();

   found_apis = g_hash_table_new (g_str_hash, g_str_equal);
   found_global_assignments = g_hash_table_new (g_str_hash, g_str_equal);
   add_hook(&match_sym, SYM_HOOK);
   add_hook(&match_assign, GLOBAL_ASSIGNMENT_HOOK);
}