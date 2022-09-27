Broiler I/O Port and MMIO Resource layout
============================

#### I/O Port

|             Broiler File            |       I/O Port Range       | PCI Vendor:Device |
| :---------------------------------- | :------------------------: | :---------------: |
| Broiler-interrup-vPIC.c             |      [0x6020, 0x6030]      |       None        |
| Broiler-interrup-vIOAPIC.c          |      [0x6040, 0x6050]      |       None        |
| Broiler-Synchronous-pio-base.c      |      [0x6060, 0x6070]      |       None        |
| Broiler-Synchronous-pio-In-Kernel.c |      [0x6080, 0x6090]      |       None        |
| Broiler-Synchronous-pio-PCI.c       |      Random                |       None        |
| Broiler-Asynchronous-pio-base.c     |      [0x60A0, 0x60B0]      |       None        |


#### MMIO

|        Broiler File                 |         MMIO Range         | PCI Vendor:Device |
| :---------------------------------- | :------------------------: | :---------------: |
| Broiler-pci-base.c                  |          Random            |     1016:1413     |
| Broiler-pci-intX-base.c             |          Random            |     1016:1991     |
| Broiler-pci-msi-base.c              |          Random            |     1001:1991     |
| Broiler-pci-msix-base.c             |          Random            |     1002:1991     |
| Broiler-DMA-intX-base.c             |          Random            |     0309:1989     |
| Broiler-DMA-msi-base.c              |          Random            |     1024:1991     |
| Broiler-DMA-msix-base.c             |          Random            |     1026:1991     |
| Broiler-Synchronous-pio-PCI.c       |          Random            |     1003:1991     |
| Broiler-Synchronous-mmio-base.c     |  [0xD0000000, 0xD0000010]  |       None        |

