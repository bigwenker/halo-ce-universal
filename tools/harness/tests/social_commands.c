#include "harness.h"
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define csstrlen strlen
static char const **enumeration_results;
static char const *hs_enumeration_substring;
static short hs_enumeration_result_count,hs_enumeration_maximum_count;
struct hs_function_definition {char const *name;};
struct console_option {char const *command;};
static struct hs_function_definition legacy={"unit_kill"};
static struct {struct hs_function_definition *functions[1];} hs_function_table={{&legacy}};
static short hs_function_table_count=1;
static struct console_option option={"viewmodel_fov"};
static struct hs_function_definition *hs_function_get(short n){return hs_function_table.functions[n];}
static struct console_option *console_option_get(unsigned n){return n==0?&option:NULL;}
#include "under_test.inc"
int main(int argc,char **argv)
{
    char const *results[8];
    CHECK(hs_find_function_by_name("unit_kill")==0,"legacy lookup changed");
    CHECK(hs_find_function_by_name("kill")==NONE,"console command became a map script");
    enumeration_results=results;hs_enumeration_maximum_count=8;hs_enumeration_substring="";
    hs_enumerate_function_names();
    CHECK(hs_enumeration_result_count==5,"missing console completions");
    CHECK(!strcmp(results[0],"kill")&&!strcmp(results[1],"suicide")&&!strcmp(results[2],"say"),"new completions");
    CHECK(!strcmp(results[3],"unit_kill")&&!strcmp(results[4],"viewmodel_fov"),"legacy/settings completions lost");
    hs_enumeration_result_count=0;hs_enumeration_substring="SU";
    hs_enumerate_function_names();
    CHECK(hs_enumeration_result_count==1&&!strcmp(results[0],"suicide"),"case insensitive prefix");
    return 0;
}
