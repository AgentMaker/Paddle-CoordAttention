// Modified from
// https://github.com/sshaoshuai/PCDet/blob/master/pcdet/ops/roiaware_pool3d/src/roiaware_pool3d_kernel.cu
// Written by Shaoshuai Shi
// All Rights Reserved 2019.

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <paddle/extension.h>

#define CHECK_INPUT(x) PD_CHECK(x.place() == paddle::PlaceType::kCPU, #x " must be a CPU Tensor.")

std::vector<paddle::Tensor> constant_tensor(int constant){
  auto constant_tensor = paddle::Tensor(paddle::PlaceType::kCPU, std::vector<int64_t> {1});
  int* data = constant_tensor.mutable_data<int>(paddle::PlaceType::kCPU);
  data[0] = constant;
  return {constant_tensor};
}

inline void lidar_to_local_coords_cpu(float shift_x, float shift_y, float rz,
                                      float &local_x, float &local_y) {
  // should rotate pi/2 + alpha to translate LiDAR to local
  float rot_angle = rz + M_PI / 2;
  float cosa = cos(rot_angle), sina = sin(rot_angle);
  local_x = shift_x * cosa + shift_y * (-sina);
  local_y = shift_x * sina + shift_y * cosa;
}

inline int check_pt_in_box3d_cpu(const float *pt, const float *box3d,
                                 float &local_x, float &local_y) {
  // param pt: (x, y, z)
  // param box3d: (cx, cy, cz, w, l, h, rz) in LiDAR coordinate, cz in the
  // bottom center
  float x = pt[0], y = pt[1], z = pt[2];
  float cx = box3d[0], cy = box3d[1], cz = box3d[2];
  float w = box3d[3], l = box3d[4], h = box3d[5], rz = box3d[6];
  cz += h / 2.0;  // shift to the center since cz in box3d is the bottom center

  if (fabsf(z - cz) > h / 2.0) return 0;
  lidar_to_local_coords_cpu(x - cx, y - cy, rz, local_x, local_y);
  float in_flag = (local_x > -l / 2.0) & (local_x < l / 2.0) &
                  (local_y > -w / 2.0) & (local_y < w / 2.0);
  return in_flag;
}

std::vector<paddle::Tensor> points_in_boxes_cpu(const paddle::Tensor& boxes_tensor, const paddle::Tensor& pts_tensor,
                                                const paddle::Tensor& pts_indices_tensor) {
  // params boxes: (N, 7) [x, y, z, w, l, h, rz] in LiDAR coordinate, z is the
  // bottom center, each box DO NOT overlaps params pts: (npoints, 3) [x, y, z]
  // in LiDAR coordinate params pts_indices: (N, npoints)

  CHECK_INPUT(boxes_tensor);
  CHECK_INPUT(pts_tensor);
  CHECK_INPUT(pts_indices_tensor);

  int boxes_num = boxes_tensor.shape[0];
  int pts_num = pts_tensor.shape[0];

  const float *boxes = boxes_tensor.data<float>();
  const float *pts = pts_tensor.data<float>();
  int *pts_indices = const_cast<int*>(pts_indices_tensor.data<int>());

  float local_x = 0, local_y = 0;
  for (int i = 0; i < boxes_num; i++) {
    for (int j = 0; j < pts_num; j++) {
      int cur_in_flag =
          check_pt_in_box3d_cpu(pts + j * 3, boxes + i * 7, local_x, local_y);
      pts_indices[i * pts_num + j] = cur_in_flag;
    }
  }

  return constant_tensor(1);
}
