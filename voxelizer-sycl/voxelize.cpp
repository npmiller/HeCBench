#include "voxelize.h"

// Set a bit in the giant voxel table. This involves doing an atomic operation on a 32-bit word in memory.
// Blocking other threads writing to it for a very short time
inline void setBit(unsigned int* voxel_table, size_t index) {
  size_t int_location = index / size_t(32);
  unsigned int bit_pos = size_t(31) - (index % size_t(32)); // we count bit positions RtL, but array indices LtR
  unsigned int mask = 1 << bit_pos;
  auto ao_ref = sycl::atomic_ref<unsigned int, 
                                 sycl::memory_order::relaxed,
                                 sycl::memory_scope::device,
                                 sycl::access::address_space::global_space> (
                                 voxel_table[int_location]);
  ao_ref.fetch_or(mask);
}

// Main triangle voxelization method
void voxelize_triangle(
  sycl::nd_item<1> &item, 
  const unsigned int *morton256_x,
  const unsigned int *morton256_y,
  const unsigned int *morton256_z,
  voxinfo info,
  float* triangle_data,
  unsigned int* voxel_table,
  bool morton_order)
{
  size_t thread_id = item.get_global_id(0);
  size_t stride = item.get_group_range(0) * item.get_local_range(0);

  // Common variables used in the voxelization process
  glm::vec3 delta_p(info.unit.x, info.unit.y, info.unit.z);
  glm::vec3 grid_max(info.gridsize.x - 1, info.gridsize.y - 1, info.gridsize.z - 1); // grid max (grid runs from 0 to gridsize-1)

  while (thread_id < info.n_triangles){ // every thread works on specific triangles in its stride
    size_t t = thread_id * 9; // triangle contains 9 vertices

    // COMPUTE COMMON TRIANGLE PROPERTIES
    // Move vertices to origin using bbox
    glm::vec3 v0 = glm::vec3(triangle_data[t], triangle_data[t + 1], triangle_data[t + 2]) - info.bbox.min;
    glm::vec3 v1 = glm::vec3(triangle_data[t + 3], triangle_data[t + 4], triangle_data[t + 5]) - info.bbox.min; 
    glm::vec3 v2 = glm::vec3(triangle_data[t + 6], triangle_data[t + 7], triangle_data[t + 8]) - info.bbox.min;
    // Edge vectors
    glm::vec3 e0 = v1 - v0;
    glm::vec3 e1 = v2 - v1;
    glm::vec3 e2 = v0 - v2;
    // Normal vector pointing up from the triangle
    glm::vec3 n = glm::normalize(glm::cross(e0, e1));

    // COMPUTE TRIANGLE BBOX IN GRID
    // Triangle bounding box in world coordinates is min(v0,v1,v2) and max(v0,v1,v2)
    AABox<glm::vec3> t_bbox_world(glm::min(v0, glm::min(v1, v2)), glm::max(v0, glm::max(v1, v2)));
    // Triangle bounding box in voxel grid coordinates is the world bounding box divided by the grid unit vector
    AABox<glm::ivec3> t_bbox_grid;
    t_bbox_grid.min = glm::clamp(t_bbox_world.min / info.unit, glm::vec3(0.0f, 0.0f, 0.0f), grid_max);
    t_bbox_grid.max = glm::clamp(t_bbox_world.max / info.unit, glm::vec3(0.0f, 0.0f, 0.0f), grid_max);

    // PREPARE PLANE TEST PROPERTIES
    glm::vec3 c(0.0f, 0.0f, 0.0f);
    if (n.x > 0.0f) { c.x = info.unit.x; }
    if (n.y > 0.0f) { c.y = info.unit.y; }
    if (n.z > 0.0f) { c.z = info.unit.z; }
    float d1 = glm::dot(n, (c - v0));
    float d2 = glm::dot(n, ((delta_p - c) - v0));

    // PREPARE PROJECTION TEST PROPERTIES
    // XY plane
    glm::vec2 n_xy_e0(-1.0f*e0.y, e0.x);
    glm::vec2 n_xy_e1(-1.0f*e1.y, e1.x);
    glm::vec2 n_xy_e2(-1.0f*e2.y, e2.x);
    if (n.z < 0.0f) {
      n_xy_e0 = -n_xy_e0;
      n_xy_e1 = -n_xy_e1;
      n_xy_e2 = -n_xy_e2;
    }
    float d_xy_e0 = (-1.0f * glm::dot(n_xy_e0, glm::vec2(v0.x, v0.y))) + glm::max(0.0f, info.unit.x*n_xy_e0[0]) + glm::max(0.0f, info.unit.y*n_xy_e0[1]);
    float d_xy_e1 = (-1.0f * glm::dot(n_xy_e1, glm::vec2(v1.x, v1.y))) + glm::max(0.0f, info.unit.x*n_xy_e1[0]) + glm::max(0.0f, info.unit.y*n_xy_e1[1]);
    float d_xy_e2 = (-1.0f * glm::dot(n_xy_e2, glm::vec2(v2.x, v2.y))) + glm::max(0.0f, info.unit.x*n_xy_e2[0]) + glm::max(0.0f, info.unit.y*n_xy_e2[1]);
    // YZ plane
    glm::vec2 n_yz_e0(-1.0f*e0.z, e0.y);
    glm::vec2 n_yz_e1(-1.0f*e1.z, e1.y);
    glm::vec2 n_yz_e2(-1.0f*e2.z, e2.y);
    if (n.x < 0.0f) {
      n_yz_e0 = -n_yz_e0;
      n_yz_e1 = -n_yz_e1;
      n_yz_e2 = -n_yz_e2;
    }
    float d_yz_e0 = (-1.0f * glm::dot(n_yz_e0, glm::vec2(v0.y, v0.z))) + glm::max(0.0f, info.unit.y*n_yz_e0[0]) + glm::max(0.0f, info.unit.z*n_yz_e0[1]);
    float d_yz_e1 = (-1.0f * glm::dot(n_yz_e1, glm::vec2(v1.y, v1.z))) + glm::max(0.0f, info.unit.y*n_yz_e1[0]) + glm::max(0.0f, info.unit.z*n_yz_e1[1]);
    float d_yz_e2 = (-1.0f * glm::dot(n_yz_e2, glm::vec2(v2.y, v2.z))) + glm::max(0.0f, info.unit.y*n_yz_e2[0]) + glm::max(0.0f, info.unit.z*n_yz_e2[1]);
    // ZX plane
    glm::vec2 n_zx_e0(-1.0f*e0.x, e0.z);
    glm::vec2 n_zx_e1(-1.0f*e1.x, e1.z);
    glm::vec2 n_zx_e2(-1.0f*e2.x, e2.z);
    if (n.y < 0.0f) {
      n_zx_e0 = -n_zx_e0;
      n_zx_e1 = -n_zx_e1;
      n_zx_e2 = -n_zx_e2;
    }
    float d_xz_e0 = (-1.0f * glm::dot(n_zx_e0, glm::vec2(v0.z, v0.x))) + glm::max(0.0f, info.unit.x*n_zx_e0[0]) + glm::max(0.0f, info.unit.z*n_zx_e0[1]);
    float d_xz_e1 = (-1.0f * glm::dot(n_zx_e1, glm::vec2(v1.z, v1.x))) + glm::max(0.0f, info.unit.x*n_zx_e1[0]) + glm::max(0.0f, info.unit.z*n_zx_e1[1]);
    float d_xz_e2 = (-1.0f * glm::dot(n_zx_e2, glm::vec2(v2.z, v2.x))) + glm::max(0.0f, info.unit.x*n_zx_e2[0]) + glm::max(0.0f, info.unit.z*n_zx_e2[1]);

    // test possible grid boxes for overlap
    for (int z = t_bbox_grid.min.z; z <= t_bbox_grid.max.z; z++){
      for (int y = t_bbox_grid.min.y; y <= t_bbox_grid.max.y; y++){
        for (int x = t_bbox_grid.min.x; x <= t_bbox_grid.max.x; x++){
          // TRIANGLE PLANE THROUGH BOX TEST
          glm::vec3 p(x*info.unit.x, y*info.unit.y, z*info.unit.z);
          float nDOTp = glm::dot(n, p);
          if ((nDOTp + d1) * (nDOTp + d2) > 0.0f) { continue; }

          // PROJECTION TESTS
          // XY
          glm::vec2 p_xy(p.x, p.y);
          if ((glm::dot(n_xy_e0, p_xy) + d_xy_e0) < 0.0f){ continue; }
          if ((glm::dot(n_xy_e1, p_xy) + d_xy_e1) < 0.0f){ continue; }
          if ((glm::dot(n_xy_e2, p_xy) + d_xy_e2) < 0.0f){ continue; }

          // YZ
          glm::vec2 p_yz(p.y, p.z);
          if ((glm::dot(n_yz_e0, p_yz) + d_yz_e0) < 0.0f){ continue; }
          if ((glm::dot(n_yz_e1, p_yz) + d_yz_e1) < 0.0f){ continue; }
          if ((glm::dot(n_yz_e2, p_yz) + d_yz_e2) < 0.0f){ continue; }

          // XZ  
          glm::vec2 p_zx(p.z, p.x);
          if ((glm::dot(n_zx_e0, p_zx) + d_xz_e0) < 0.0f){ continue; }
          if ((glm::dot(n_zx_e1, p_zx) + d_xz_e1) < 0.0f){ continue; }
          if ((glm::dot(n_zx_e2, p_zx) + d_xz_e2) < 0.0f){ continue; }

          if (morton_order){
            size_t location = mortonEncode_LUT(morton256_x, morton256_y, morton256_z, x, y, z);
            setBit(voxel_table, location);
          } else {
            size_t location = static_cast<size_t>(x) + (static_cast<size_t>(y) * static_cast<size_t>(info.gridsize.y)) + 
                             (static_cast<size_t>(z)* static_cast<size_t>(info.gridsize.y)* static_cast<size_t>(info.gridsize.z));
            setBit(voxel_table, location);
          }
          continue;
        }
      }
    }
    thread_id += stride;
  }
}

void voxelize(sycl::queue &q, const voxinfo& v, float* triangle_data, unsigned int* vtable, bool morton_code) {

  unsigned int *morton256_x = sycl::malloc_device<unsigned int>(256, q);
  unsigned int *morton256_y = sycl::malloc_device<unsigned int>(256, q);
  unsigned int *morton256_z = sycl::malloc_device<unsigned int>(256, q);

  // Copy morton LUT if we're encoding to morton
  if (morton_code) {
    q.memcpy(morton256_x, host_morton256_x, 256 * sizeof(unsigned int));
    q.memcpy(morton256_y, host_morton256_y, 256 * sizeof(unsigned int));
    q.memcpy(morton256_z, host_morton256_z, 256 * sizeof(unsigned int));
  }

  // Round up according to array size 
  const int blockSize = 256;
  const int gridSize = (v.n_triangles + blockSize - 1) / blockSize;

  sycl::range<1> gws (gridSize * blockSize);
  sycl::range<1> lws (blockSize);

  size_t vtable_size = ((size_t)v.gridsize.x * v.gridsize.y * v.gridsize.z) / (size_t) 8.0;
  printf("[Voxel Grid] Allocating %zu kB of DEVICE memory for Voxel Grid\n", size_t(vtable_size / 1024.0f));

  unsigned int* dev_vtable = (unsigned int*) sycl::malloc_device (vtable_size, q);
  q.memset(dev_vtable, 0, vtable_size);
  q.wait();

  // Start voxelization
  auto k = q.submit([&] (sycl::handler &cgh) {
    cgh.parallel_for<class vx>(sycl::nd_range<1>(gws, lws), [=] (sycl::nd_item<1> item) {
      voxelize_triangle(item, morton256_x, morton256_y, morton256_z,
                        v, triangle_data, dev_vtable, morton_code);
    });
  });

  // Copy the voxel table back and free all
  printf("[Voxel Grid] Copying %zu kB to page-locked HOST memory\n", size_t(vtable_size / 1024.0f));
  q.memcpy(vtable, dev_vtable, vtable_size, k).wait();

  printf("[Voxel Grid] Freeing %zu kB of DEVICE memory\n", size_t(vtable_size / 1024.0f));
  sycl::free(dev_vtable, q);
}
