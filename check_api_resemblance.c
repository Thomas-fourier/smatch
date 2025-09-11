#include "smatch.h"
#include "smatch_extra.h"
#include <glib.h>
#include <math.h>

 #define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define nb_max_pair 200

typedef float score;

GHashTable *function_calls = NULL;
FILE *out;

static void match_func(struct expression *expr)
{
    if (__inline_call)
        return;

    if (ptr_list_size((struct ptr_list *)expr->args) <= 1)
        return;

    char *call_expr = expr_to_str(expr);
    if (!call_expr)
        return;

    bool free_fn = false;
    char *fn = expr_to_str(expr->fn);
    struct string_list *tab = g_hash_table_lookup(function_calls, fn);
    if (tab)
        free_fn = true;

    add_ptr_list(&tab, call_expr);

    g_hash_table_insert(function_calls, fn, tab);
    if (free_fn)
        free(fn);
}


unsigned long long levenshtein_d(const char *s, const char *t)
{
	unsigned long long k, i, j, n, m, cost, *d, distance, a, b, c;

	//Step 1
	n = strlen(s);
	m = strlen(t);
	if (n != 0 && m != 0) {
		d = malloc((sizeof(unsigned long long)) * (m + 1) * (n + 1));
		m++;
		n++;
		//Step 2
		for (k = 0; k < n; k++)
			d[k] = k;
		for (k = 0; k < m; k++)
			d[k * n] = k;
			//Step 3 and 4
        for (i = 1; i < n; i++)
            for (j = 1; j < m; j++) {
                //Step 5
                if (s[i - 1] == t[j - 1])
                    cost = 0;
                else
                    cost = 1;
                //Step 6
                a = d[(j - 1) * n + i] + 1;
                b = d[j * n + i - 1] + 1;
                c = d[(j - 1) * n + i - 1] + cost;
                d[j * n + i] = (min(a,(min(b,c))));
            }
			distance = d[n * m - 1];
		free(d);
		return distance;
	} else
		return (max(m,n));
	// return the full string cost if one is zero length
}


static score compute_correlation(struct string_list *calls_i,
                                 struct string_list *calls_j)
{
    char *i, *j;
    int ind_i, ind_j;
    score cur_min;
    score avg_i = 0, avg_j = 0;
    int len_i, len_j;
    score **dists;

    len_i = ptr_list_size((struct ptr_list *)calls_i);
    len_j = ptr_list_size((struct ptr_list *)calls_j);

    dists = malloc(sizeof(*dists) * len_i);
    for (ind_i = 0; ind_i < len_i; ind_i++)
        dists[ind_i] = malloc(sizeof(**dists) * len_j);

    ind_i = 0;
    FOR_EACH_PTR(calls_i, i) {
        ind_j = 0;
        FOR_EACH_PTR(calls_j, j) {
            dists[ind_i][ind_j] = levenshtein_d(i, j);
            ind_j++;
        } END_FOR_EACH_PTR(j);
        ind_i++;
    } END_FOR_EACH_PTR(i);

    ind_i = 0;
    FOR_EACH_PTR(calls_i,i); {
        cur_min = INFINITY;
        for (ind_j = 0; ind_j < len_j; ind_j++)
            cur_min = min((score)cur_min, (score)dists[ind_i][ind_j] / (score)strlen(i));
        avg_i += cur_min;
        ind_i++;
    } END_FOR_EACH_PTR(i);

    ind_j = 0;
    FOR_EACH_PTR(calls_j, j) {
        cur_min = INT_MAX;
        for (ind_i = 0; ind_i < len_i; ind_i++) {
            cur_min = min((score)cur_min, (score)dists[ind_i][ind_j] / (score)strlen(j));
        }
        avg_j += cur_min;
        ind_j++;
    } END_FOR_EACH_PTR(j);

    
    for (ind_i = 0; ind_i < len_i; ind_i++)
        free(dists[ind_i]);
    free(dists);


    return avg_i / (score)len_i + avg_j / (score)len_j;
}

static void add_to_dist(char *fun_i, char *fun_j, score dist, score *distances,
                        char **func_pair)
{
    if (dist > distances[nb_max_pair - 1])
        return;

    int j = nb_max_pair - 1, i = 0;
    while (dist > distances[i] && i < nb_max_pair)
        i++;

    free(func_pair[j]);

    while (j >= i) {
        distances[j] = distances[j - 1];
        func_pair[j] = func_pair[j - 1];
        j--;
    }

    distances[i] = dist;
    asprintf(&func_pair[i], "%s %s", fun_i, fun_j);
}

static void match_file_end()
{
    GHashTableIter i, j;
    char *fun_i, *fun_j;
    struct string_list *calls_i, *calls_j;
    score distances[nb_max_pair];
    char *func_pair[nb_max_pair];

    memset(distances, 0xff, nb_max_pair * sizeof(*distances));
    memset(func_pair, 0x00, nb_max_pair * sizeof(*func_pair));

    g_hash_table_iter_init(&i, function_calls);
    
    while (g_hash_table_iter_next(&i, (void **)&fun_i,
                                  (void **)&calls_i)) {
        g_hash_table_iter_remove(&i); // Remove to compute distance 
        // between different functions
        g_hash_table_iter_init(&j, function_calls);
        while (g_hash_table_iter_next(&j, (void **)&fun_j,
                                      (void **)&calls_j)) {
            score dis = compute_correlation(calls_i, calls_j);
            add_to_dist(fun_i, fun_j, dis, distances, func_pair);
        }

        free(fun_i);
        FOR_EACH_PTR(calls_i, fun_i) {
            free(fun_i);
        } END_FOR_EACH_PTR(fun_i);
        free_ptr_list(&calls_i);
    }

    for (int i = 0; i < nb_max_pair && func_pair[i]; i++) {
        fprintf(out, "funct pair: %s %f\n", func_pair[i], distances[i]);
        free(func_pair[i]);
    }
}

void check_api_resemblance(int id)
{
    out = stderr;
    function_calls = g_hash_table_new(g_str_hash, g_str_equal);

    add_hook(match_func, FUNCTION_CALL_HOOK);
    add_hook(match_file_end, END_FILE_HOOK);
}