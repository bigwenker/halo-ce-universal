"""Exercise the production loose-JMS parser with valid and corrupt files."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,run,mutated

def executable(fault=False):
    code=read('port/linux/src/custom_model_jms.c')
    if fault:code=mutated(code,'if(v<low || v>high)','if(0)')
    return build('custom_models_jms',(('halo_custom_models.h',read('port/linux/include/halo_custom_models.h')),('jms.inc',code)))

def fixture():
    lines=['8200','1828349080','19']
    for i in range(19):lines += [f'bone {i}',str(i+1 if i<18 else -1),'-1','0 0 0 1','0 0 0']
    lines += ['1','armor','<none>','0','1','cyborg','3']
    for i in range(3):lines += ['0',f'{i} 0 0','0 0 1','-1','0','0 0','0']
    return '\n'.join(lines+['1','0','0','0 1 2'])+'\n'

@pytest.mark.parametrize('case',['valid','truncated','bad_weight','nan','bad_index','bad_count','cycle','material','trailing','nul'])
def test_parser(tmp_path,case):
    data=fixture()
    if case=='truncated':data=data[:-15]
    elif case=='bad_weight':data=data.replace('\n-1\n0\n0 0\n','\n-1\n1.5\n0 0\n',1)
    elif case=='nan':data=data.replace('0 0 1\n-1','nan 0 1\n-1',1)
    elif case=='bad_index':data=data.replace('0 1 2\n','0 1 65535\n')
    elif case=='bad_count':data=data.replace('\ncyborg\n3\n','\ncyborg\n2147483647\n')
    elif case=='cycle':data=data.replace('bone 18\n-1\n-1','bone 18\n0\n-1')
    elif case=='material':data=data.replace('\narmor\n','\nvisor\n')
    elif case=='trailing':data+='garbage\n'
    elif case=='nul':data=data.replace('armor','ar\0or')
    path=tmp_path/'model.jms';path.write_bytes(data.encode())
    code,out=run(executable(),str(path))
    assert code==(0 if case=='valid' else 1),out

def test_negative_weight_control(tmp_path):
    data=fixture().replace('\n-1\n0\n0 0\n','\n0\n1.5\n0 0\n',1)
    path=tmp_path/'model.jms';path.write_text(data)
    assert run(executable(),str(path))[0]==1
    assert run(executable(True),str(path))[0]==0
