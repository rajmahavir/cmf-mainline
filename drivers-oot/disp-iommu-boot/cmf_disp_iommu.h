/* SPDX-License-Identifier: GPL-2.0 */
/* Map API of cmf_disp_iommu_boot for Linux-owned scanout buffers (bank 0). */
#ifndef CMF_DISP_IOMMU_H
#define CMF_DISP_IOMMU_H

#include <linux/types.h>

struct page;

bool cmf_dispiommu_attached(void);
int cmf_dispiommu_map_pages(unsigned long iova, struct page **pages, unsigned int npages);
int cmf_dispiommu_unmap(unsigned long iova, size_t size);
u64 cmf_dispiommu_lookup(unsigned long iova);
bool cmf_dispiommu_faults(void);

#endif
