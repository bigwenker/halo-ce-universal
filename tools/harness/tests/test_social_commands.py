"""Real command enumeration must not run during map-script function lookup."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run

def test_command_enumeration():
    source=read('source/hs/hs.c')
    code='\n'.join(function(source,name) for name in (
        'hs_tokens_enumerate_add_string','hs_find_function_by_name','hs_enumerate_function_names'))
    status,output=run(build('social_commands',(('under_test.inc',code),)),'enumerate')
    assert status==0,output
