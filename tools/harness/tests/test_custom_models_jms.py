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
    for position in ['0 0 0','100 0 0','0 100 0']:lines += ['0',position,'0 0 1','-1','0','0 0','0']
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


@pytest.mark.parametrize('reverse',[False,True])
def test_native_face_winding_preserves_normals(tmp_path,reverse):
    # A +Z authoring face must become Halo's clockwise native face. Keep
    # per-vertex data intact; changing its normal cannot repair culling.
    data=fixture()
    if reverse:data=data.replace('0 1 2\n','0 2 1\n')
    path=tmp_path/'face.jms';path.write_text(data)
    code,out=run(executable(),str(path))
    assert code==0,out
    lines=out.splitlines()
    vertices=[list(map(float,line.split()[1:])) for line in lines if line.startswith('v ')]
    triangle=next(list(map(int,line.split()[1:])) for line in lines if line.startswith('t '))
    assert [v[:3] for v in vertices]==[[0,0,0],[1,0,0],[0,1,0]]
    assert all(v[3:]==[0,0,1,0,1] for v in vertices)
    a,b,c=[vertices[i] for i in triangle]
    # The native cross product opposes the outward normal (verified against
    # the stock compiled Chief); an intentionally reversed input stays so.
    cross_z=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    assert cross_z==(1 if reverse else -1),out
