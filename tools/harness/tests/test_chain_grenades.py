"""Loose-grenade provenance survives item deletion without changing thrown grenades."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,structure,run,mutated,CHECK_FAILED

def generated(fault=None):
    items=read('source/items/items.c');effects=read('source/effects/effects.c')
    code='\n'.join(function(items,n) for n in ['item_accelerate_from_damage','item_is_chain_reaction_grenade','item_detonate'])
    helper=structure(effects,'effect_chain_owner')+'\nstatic struct effect_chain_owner effect_chain_owners[HALO_PORT_MAXIMUM_EFFECTS];\n'
    helper+='\n'.join(function(effects,n) for n in ['effects_chain_reaction_reset','effect_chain_slot','effect_chain_capture','effect_chain_damage'])
    allocate=function(effects,'effect_allocate')
    capture=allocate[allocate.index('effect->definition_index = definition_index;'):allocate.index('effect_set_event(effect_index, 0);')]
    part=function(effects,'effect_generate_part')
    blast=part[part.index('case DAMAGE_EFFECT_DEFINITION_TAG:'):part.index('case LIGHT_DEFINITION_TAG:')]
    blast=blast[blast.index('{'):blast.rfind('}')+1].replace('\n\t\t\tbreak;','')
    if fault=='provenance':capture=mutated(capture,'FLAG(_effect_chain_reaction_bit) : 0','0 : 0')
    if fault=='instigator':helper=mutated(helper,'saved->object_index = item->object.owner_object_index;','saved->object_index = owner_index;')
    return (('items.inc',code),('capture.inc',capture),('blast.inc',blast),('helpers.inc',helper))

@pytest.mark.parametrize('case',['chain','ordinary','guards','reuse','restore'])
def test_case(case):
    status,output=run(build('chain_grenades',generated()),case)
    assert status==0,output

@pytest.mark.parametrize('fault',['provenance','instigator'])
def test_negative(fault):
    status,output=run(build('chain_grenades',generated(fault)),'chain')
    assert status==CHECK_FAILED,output
