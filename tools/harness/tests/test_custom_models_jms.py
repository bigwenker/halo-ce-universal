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
    elif case=='material':data=data.replace('\narmor\n','\n../visor\n')
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

@pytest.mark.parametrize('bad',[None,'index','duplicate','count'])
def test_material_assignments(tmp_path,bad):
    data=fixture().replace('1\narmor\n<none>\n','2\nbody\nC:\\author\\body_d.bmp\nface.bmp\n<none>\n')
    data=data.rsplit('1\n0\n0\n0 1 2\n',1)[0]+'2\n0\n0\n0 1 2\n0\n1\n2 1 0\n'
    if bad=='index':data=data.replace('0\n1\n2 1 0','0\n2\n2 1 0')
    if bad=='duplicate':data=data.replace('face.bmp','body')
    if bad=='count':data=data.replace('2\nbody\n','33\nbody\n')
    path=tmp_path/'materials.jms';path.write_text(data)
    status,output=run(executable(),str(path))
    assert status==(1 if bad else 0),output
    if not bad:
        assert 'm 0 body body_d' in output and 'm 1 face.bmp face' in output
        assert 'p 0 3 1' in output and 'p 1 3 1' in output
        assert 'a 0 0' in output and 'a 1 1' in output

@pytest.mark.parametrize('materials',[1,2])
def test_large_mesh_compacts_each_material(tmp_path,materials):
    data=fixture();header=data[:data.index('\ncyborg\n3\n')]+ '\ncyborg\n65538\n'
    if materials==2:header=header.replace('1\narmor\n<none>\n','2\narmor\n<none>\nface\n<none>\n')
    vertex='0\n0 0 0\n0 0 1\n-1\n0\n0 0\n0\n'
    triangles=''.join(f'0\n{i%materials}\n{i*3} {i*3+1} {i*3+2}\n' for i in range(21846))
    path=tmp_path/'large.jms';path.write_text(header+vertex*65538+'21846\n'+triangles)
    status,output=run(executable(),str(path))
    assert status==(0 if materials==2 else 2),output[-1000:]
    if materials==2:assert 'p 0 32769 10923' in output and 'p 1 32769 10923' in output


def test_legacy_armor_ignores_author_machine_bitmap_hint(tmp_path):
    data=fixture().replace('<none>','C:\\author\\old_sm')
    path=tmp_path/'legacy.jms';path.write_text(data)
    status,output=run(executable(),str(path))
    assert status==0 and 'm 0 armor armor' in output
