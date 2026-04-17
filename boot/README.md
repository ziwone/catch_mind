# U-Boot 설정

## bootargs 변경
`nomodeset` 추가: vc4-drm이 simple-framebuffer를 해제하는 문제 방지.

```
U-Boot> setenv bootargs 'console=ttyS0,115200n81 rootfstype=ext2 root=/dev/ram0 rw initrd=0x7000000,128M nomodeset'
U-Boot> saveenv
U-Boot> boot
```

## 업데이트 명령어 (tftp → MMC 저장)

```
U-Boot> run Image
U-Boot> run DTB
U-Boot> run RFS
```