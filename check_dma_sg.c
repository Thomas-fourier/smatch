#include "smatch.h"
#include "smatch_extra.h"
#include <string.h>

static int my_id;

static const char *dma_sg_mapping_funcs[] = {
    "dma_map_sg_attrs",
    "dma_map_sgtable",
    "dma_map_sg",
};

static const char *dma_sg_unmap_funcs[] = {
    "dma_unmap_sg_attrs",
    "dma_unmap_sgtable",
    "dma_unmap_sg",
};

static struct string_list *sg;      // arg 2
static struct string_list *nents;   // arg 3, number of entries
static struct string_list *nmaped;  // ret value: number of mapped regions

static bool is_expr_in_list(struct expression *expr, struct string_list *list)
{
    char *ptr;
    if (expr->type == EXPR_DEREF && expr->member) {
        FOR_EACH_PTR(list, ptr) {
            if (strcmp(expr->member->name, ptr) == 0)
                return true;
        } END_FOR_EACH_PTR(ptr);
    } else {
        char *expr_str = expr_to_str(expr);
        FOR_EACH_PTR(list, ptr) {
            if (strcmp(expr_str, ptr) ==  0) {
                free_string(expr_str);
                return true;
            }
        } END_FOR_EACH_PTR(ptr);
        free_string(expr_str);
    }
    return false;
}

static void add_expr_to_list(struct expression *expr, struct string_list **list)
{
    if (is_expr_in_list(expr, *list))
        return;

    if (expr->type == EXPR_DEREF && expr->member) {
        insert_string(list, expr->member->name);
    } else {
        insert_string(list, expr_to_str(expr));
    }

}


static void generic_dma_sg(struct expression *expr)
{
    struct expression *this_sg = get_argument_from_call_expr(expr->args, 1);
    add_expr_to_list(this_sg, &sg);

    struct expression *this_nents = get_argument_from_call_expr(expr->args, 2);
    if (is_expr_in_list(this_nents, nmaped))
        sm_warning("Possibly mixing nents and number of mapped regions.");
    add_expr_to_list(this_nents, &nents);   
}


static void match_dma_sg_map(const char *name, struct expression *expr, void *_)
{
    generic_dma_sg(expr);

    struct expression *parent = expr_get_parent_expr(expr);
    if (!parent)
        return;

    if (parent->type == EXPR_ASSIGNMENT && parent->left) {
        if (is_expr_in_list(parent->left, nents)) {
            sm_warning("Possibly mixing nents and number of mapped regions.");
        }
        add_expr_to_list(parent->left, &nmaped);
    }

    return;
}


static void match_dma_sg_unmap(const char *name, struct expression *expr, void *_)
{
    generic_dma_sg(expr);
}

void check_dma_sg(int id) {
    my_id = id;


    for (int i = 0; i < ARRAY_SIZE(dma_sg_unmap_funcs); i++)
        add_function_hook(dma_sg_unmap_funcs[i], match_dma_sg_unmap, NULL);

    for (int i = 0; i < ARRAY_SIZE(dma_sg_mapping_funcs); i++)
        add_function_hook(dma_sg_mapping_funcs[i], match_dma_sg_map, NULL);

}