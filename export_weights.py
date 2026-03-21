#!/usr/bin/env python3
"""
Exporte les poids du modèle TCN (cpc_model.pt) vers un header C++.
Génère : export/tcn_weights.h
"""

import torch

MODEL_PATH  = 'ml_env/cpc_model.pt'
OUTPUT_PATH = 'export/tcn_weights.h'

# Correspondance key PyTorch → nom C++ + métadonnées
LAYERS = [
    ('conv.0.weight', 'conv0_weight', (16,  1, 9), 'Conv1d  1→16, k=9'),
    ('conv.0.bias',   'conv0_bias',   (16,),        None),
    ('conv.2.weight', 'conv2_weight', (32, 16, 9), 'Conv1d 16→32, k=9'),
    ('conv.2.bias',   'conv2_bias',   (32,),        None),
    ('conv.4.weight', 'conv4_weight', (16, 32, 9), 'Conv1d 32→16, k=9'),
    ('conv.4.bias',   'conv4_bias',   (16,),        None),
    ('head.weight',   'head_weight',  (1, 16),      'Linear 16→1'),
    ('head.bias',     'head_bias',    (1,),          None),
]

def format_floats(values, cols=8):
    lines = []
    for i in range(0, len(values), cols):
        chunk = values[i:i+cols]
        lines.append('    ' + ', '.join(f'{v:.8f}f' for v in chunk))
    return ',\n'.join(lines)

state = torch.load(MODEL_PATH, map_location='cpu')

with open(OUTPUT_PATH, 'w', encoding='utf-8') as f:
    f.write('// Auto-généré par export_weights.py — NE PAS ÉDITER\n')
    f.write('// Modèle : TinyCpaTCN  (3× Conv1d k=9 + Linear)\n')
    f.write('#pragma once\n\n')
    f.write('namespace TcnWeights {\n\n')

    for key, cname, shape, comment in LAYERS:
        tensor = state[key]
        assert list(tensor.shape) == list(shape), \
            f'{key}: shape mismatch {tensor.shape} vs {shape}'
        values = tensor.numpy().flatten().tolist()
        n = len(values)
        if comment:
            f.write(f'// {comment}\n')
        f.write(f'static const float {cname}[{n}] = {{\n')
        f.write(format_floats(values))
        f.write('\n};\n\n')

    f.write('} // namespace TcnWeights\n')

print(f'Généré : {OUTPUT_PATH}  ({sum(len(state[k].numpy().flatten()) for k,*_ in LAYERS)} floats)')
