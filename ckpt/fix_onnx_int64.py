import sys
import numpy as np
import onnx
from onnx import numpy_helper, TensorProto

input_path = sys.argv[1]
output_path = sys.argv[2]

model = onnx.load(input_path)
count = 0

for initializer in model.graph.initializer:
    if initializer.data_type == TensorProto.INT64:
        arr = numpy_helper.to_array(initializer)
        if arr.size == 0:
            continue
        if arr.min() < np.iinfo(np.int32).min or arr.max() > np.iinfo(np.int32).max:
            print(f"[WARN] skip initializer {initializer.name}, out of int32 range")
            continue
        initializer.CopyFrom(numpy_helper.from_array(arr.astype(np.int32), initializer.name))
        count += 1

for node in model.graph.node:
    if node.op_type == "Constant":
        for attr in node.attribute:
            if attr.type == onnx.AttributeProto.TENSOR and attr.t.data_type == TensorProto.INT64:
                arr = numpy_helper.to_array(attr.t)
                if arr.size == 0:
                    continue
                if arr.min() < np.iinfo(np.int32).min or arr.max() > np.iinfo(np.int32).max:
                    print(f"[WARN] skip Constant {node.name}, out of int32 range")
                    continue
                attr.t.CopyFrom(numpy_helper.from_array(arr.astype(np.int32), attr.t.name))
                count += 1

onnx.checker.check_model(model)
onnx.save(model, output_path)

print(f"Converted {count} INT64 tensors/constants")
print(f"Saved: {output_path}")