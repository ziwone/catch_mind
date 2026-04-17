# U-Boot 설정

## 커널 크기 증가에 따른 bootcmd 변경

커널 크기가 증가하면서 기존 DTB/rootfs 로드 주소(0x3100000, 0x3200000)가
커널 재배치 영역(0x200000 ~ 0x5080000)과 겹치는 문제 발생.
DTB와 rootfs 주소를 0x5080000 이상으로 이동.

`nomodeset` 추가: vc4-drm이 simple-framebuffer를 해제하는 문제 방지.

```
U-Boot> setenv bootcmd 'fatload mmc 0:1 80000 Image; fatload mmc 0:1 6000000 wt2837.dtb; fatload mmc 0:1 7000000 ext2img.gz; booti 80000 - 6000000'
U-Boot> setenv bootargs 'console=ttyS0,115200n81 rootfstype=ext2 root=/dev/ram0 rw initrd=0x7000000,128M nomodeset'
U-Boot> saveenv
U-Boot> boot
```

## 메모리 맵

| 항목 | 주소 |
|---|---|
| Image 로드 | 0x0008_0000 |
| Image 재배치 | 0x0020_0000 ~ 0x0508_0000 |
| DTB | 0x0600_0000 |
| rootfs (initrd) | 0x0700_0000 |

## 업데이트 명령어 (tftp → MMC 저장)

```
U-Boot> run Image
U-Boot> run DTB
U-Boot> run RFS
```