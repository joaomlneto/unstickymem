#include <unistd.h>
#include <sys/syscall.h>
#include <numeric>
#include <iostream>
#include "unstickymem/Logger.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/MemoryMap.hpp"

namespace unstickymem {

void place_on_node(char *addr, unsigned long len, int node) {
	DIEIF(node < 0 || node >= numa_num_configured_nodes(),
			"invalid NUMA node id");
	struct bitmask *nodemask = numa_bitmask_alloc(numa_num_configured_nodes());
	numa_bitmask_setbit(nodemask, node);
	DIEIF(
			mbind(addr, len, MPOL_BIND, nodemask->maskp, nodemask->size + 1,
					MPOL_MF_MOVE | MPOL_MF_STRICT) != 0, "mbind error");
}

void force_uniform_interleave(char *addr, unsigned long len) {
	const size_t len_per_call = 64 * PAGE_SIZE;
	int num_nodes = numa_num_configured_nodes();

	// validate input
	DIEIF(len % PAGE_SIZE != 0,
			"Size of region must be a multiple of the page size");

	// compute nodemasks for each node
	std::vector<struct bitmask *> nodemasks(num_nodes);
	for (int i = 0; i < num_nodes; i++) {
		nodemasks[i] = numa_bitmask_alloc(num_nodes);
		numa_bitmask_setbit(nodemasks[i], i);
	}

	// interleave all pages through all nodes
	int node_to_bind = 0;
	while (len > 0) {
		unsigned long mbind_len = std::min(len_per_call, len);
		/*LTRACEF("mbind(%p, %lu, MPOL_BIND, 0x%x, %d, MPOL_MF_MOVE | MPOL_MF_STRICT)",
		 addr, mbind_len, *(nodemasks[node_to_bind]->maskp),
		 nodemasks[node_to_bind]->size + 1);*/
		DIEIF(
				mbind(addr, mbind_len, MPOL_BIND,
						nodemasks[node_to_bind]->maskp,
						nodemasks[node_to_bind]->size + 1,
						MPOL_MF_MOVE | MPOL_MF_STRICT) != 0, "mbind error");
		addr += mbind_len;
		len -= mbind_len;
		node_to_bind = (node_to_bind + 1) % num_nodes;
	}

	// free resources
	for (int i = 0; i < num_nodes; i++) {
		numa_bitmask_free(nodemasks[i]);
	}
}

void force_uniform_interleave(MemorySegment &segment) {
	force_uniform_interleave(reinterpret_cast<char*>(segment.startAddress()),
			segment.length());
}

/*
 void place_pages2(MemorySegment &segment, double ratios[], int ratios_size) {
 //void *addr = segment.startAddress();
 unsigned long len = segment.length();
 // validate input
 DIEIF(ratios_size != numa_num_configured_nodes(),
 "Must specify one ratio for each of the NUMA nodes configured");
 DIEIF(len % PAGE_SIZE != 0,
 "Size of region must be a multiple of the page size");
 //LDEBUGF("sum is %lf", std::accumulate(&ratios[0], &ratios[ratios_size], 0.0));
 // FIXME(joaomlneto): working with doubles is a pain; alternatives?
 DIEIF(std::accumulate(&ratios[0], &ratios[ratios_size], 0.0) < 0.99 ||
 std::accumulate(&ratios[0], &ratios[ratios_size], 0.0) > 1.01,
 "Sum of ratios must be ~1");
 char *addr = reinterpret_cast<char*>(segment.startAddress());
 segment.print();
 for (int i=0; i < ratios_size; i++) {
 unsigned long len_unaligned = segment.length() * ratios[i];
 unsigned long len_bind = std::min(len, len_unaligned + PAGE_SIZE - len_unaligned % PAGE_SIZE);
 LINFOF("binding %lu pages (~%lf%%) to node %d",
 len_bind / PAGE_SIZE, (double)len_bind / segment.length(), i);
 //DIEIF(mbind(addr, len_bind, MPOL_));
 len -= len_bind;
 addr += len_bind;
 }
 DIEIF(len != 0, "we didnt do a good job splitting the pages!");
 }

 void place_pages2(MemorySegment &segment, double ratio) {
 std::vector<double> ratios(numa_num_configured_nodes());
 ratios[0] = ratio;
 for (size_t i=1; i<ratios.size(); i++) {
 ratios[i] = (1 - ratio) / (numa_num_configured_nodes() - 1);
 }
 for (auto i: ratios)
 std::cout << i << ' ';
 std::cout << std::endl;
 place_pages2(segment, ratios.data(), ratios.size());
 }
 */

void place_pages(void *addr, unsigned long len, double r) {
	// compute the ratios to input to `mbind`
	double local_ratio = r - (1.0 - r) / (numa_num_configured_nodes() - 1);
	double interleave_ratio = 1.0 - local_ratio;
	// compute the lengths of the interleaved and local segments
	unsigned long interleave_len = interleave_ratio * len;
	interleave_len &= PAGE_MASK;
	unsigned long local_len = (len - interleave_len) & PAGE_MASK;
	// the starting address for the local segment
	void *local_addr = ((char*) addr) + interleave_len;

	// validate input
	DIEIF(r < 0.0 || r > 1.0, "specified ratio must be between 0 and 1!");
	DIEIF(local_ratio < 0.0 || local_ratio > 1.0,
			"bad local_ratio calculation");
	DIEIF(interleave_ratio < 0.0 || interleave_ratio > 1.0,
			"bad interleave_ratio calculation");
	DIEIF(local_len % PAGE_SIZE != 0,
			"local length must be multiple of the page size");
	DIEIF(interleave_len % PAGE_SIZE != 0,
			"interleave length must be multiple of the page size");
	DIEIF((local_len + interleave_len) != (len & PAGE_MASK),
			"local and interleave lengths should total the original length");

	// interleave some portion of the memory segment between all NUMA nodes
	/*LTRACEF("mbind(%p, %lu, MPOL_INTERLEAVE, numa_get_mems_allowed(), MPOL_MF_MOVE | MPOL_MF_STRICT)",
	 addr, interleave_len);
	 DIEIF(mbind(addr, interleave_len, MPOL_INTERLEAVE, numa_get_mems_allowed()->maskp,
	 numa_get_mems_allowed()->size + 1, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
	 "mbind interleave failed");*/

	// check if there is something left to bind to local
	if (local_len <= 0)
		return;

	// bind the remainder to the local node
	LTRACEF(
			"mbind(%p, %lu, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT)",
			local_addr, local_len);
	DIEIF(
			mbind(local_addr, local_len, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
			"mbind local failed");
}

void place_pages(MemorySegment &segment, double ratio) {
	// LDEBUGF("segment %s [%p:%p] ratio: %lf", segment.name().c_str(), segment.startAddress(), segment.endAddress(), ratio);
	DIEIF(!segment.isBindable(), "trying to bind non-bindable segment!");
	place_pages(segment.startAddress(), segment.length(), ratio);
}

void place_all_pages(MemoryMap &segments, double ratio) {
	for (auto &segment : segments) {
		if (segment.isBindable() && segment.isWriteable()
				&& segment.length() > 1ULL << 20) {
			place_pages(segment, ratio);
		}
	}
}

void place_all_pages(double ratio) {
	LDEBUGF("place_pages with local ratio %lf", ratio);
	MemoryMap segments;
	place_all_pages(segments, ratio);
}

}  // namespace unstickymem
