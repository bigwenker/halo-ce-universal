"""Named body/FP authoring paths are independent; obsolete LOD files are ignored."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import read, function, build, run
from test_custom_models_jms import fixture


def executable():
    source=read('port/linux/src/custom_model_files.c')
    code='\n'.join(function(source,n) for n in ('read_bounded','halo_custom_mesh_load'))
    return build('custom_model_paths', (
        ('halo_custom_models.h',read('port/linux/include/halo_custom_models.h')),
        ('jms.inc',read('port/linux/src/custom_model_jms.c')),('paths.inc',code)))


@pytest.mark.parametrize('kind',[0,1,-1,2])
def test_named_paths(tmp_path,kind):
    body=tmp_path/'model/source/characters/cyborg/models';body.mkdir(parents=True)
    fp=body.parent/'fp/models';fp.mkdir(parents=True)
    (body/'model.jms').write_text(fixture())
    (fp/'model.jms').write_text(fixture(37).replace('0 0 0\n0 0 1','50 0 0\n0 0 1',1))
    # Deliberately corrupt obsolete files; the named model remains authoritative.
    (body/'__base superhigh.jms').write_text('invalid')
    import subprocess
    result=subprocess.run([str(executable()),str(tmp_path),str(kind)],capture_output=True,text=True)
    assert result.returncode==(0 if kind in (0,1) else 1),result.stderr
    if result.returncode==0:assert float(result.stdout)==(0 if kind==0 else .5)


def test_legacy_only_is_not_loaded(tmp_path):
    body=tmp_path/'model/source/characters/cyborg/models';body.mkdir(parents=True)
    (body/'__base superhigh.jms').write_text(fixture())
    import subprocess
    assert subprocess.run([str(executable()),str(tmp_path),'0']).returncode==1
