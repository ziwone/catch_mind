#!/bin/bash

echo "==================================="
echo "NFS 상태 진단 스크립트"
echo "==================================="

# 1. NFS 서버 상태 확인
echo ""
echo "1. NFS 서버 프로세스 확인:"
ps aux | grep nfsd || echo "  ⚠️ NFS 서버 프로세스 없음"

# 2. /nfsroot 디렉토리 확인
echo ""
echo "2. /nfsroot 디렉토리 내용:"
ls -alh /nfsroot/ 2>/dev/null || echo "  ⚠️ /nfsroot 접근 불가"

# 3. NFS export 설정 확인
echo ""
echo "3. NFS export 설정 (/etc/exports):"
cat /etc/exports 2>/dev/null || echo "  ⚠️ /etc/exports 파일 없음"

# 4. 실제 export 상태 확인
echo ""
echo "4. 현재 export 상태:"
sudo exportfs -v 2>/dev/null || echo "  ⚠️ exportfs 명령 실행 불가"

# 5. 네트워크에서 보이는 NFS 공유
echo ""
echo "5. NFS 공유 목록:"
showmount -e localhost 2>/dev/null || echo "  ⚠️ showmount 실행 불가"

# 6. 보드에서 마운트 테스트 (192.168.10.3 예시)
echo ""
echo "6. 보드 192.168.10.3 NFS 마운트 상태 확인:"
{
    echo "mount | grep nfs"
    echo "ls -alh /mnt/nfs/ 2>/dev/null || echo 'NFS not mounted'"
    echo "exit"
} | telnet 192.168.10.3 2>/dev/null | grep -A 5 "nfs\|NFS"

echo ""
echo "==================================="
echo "진단 제안:"
echo "==================================="
echo ""
echo "만약 NFS 서버가 없다면:"
echo "  sudo apt install nfs-kernel-server"
echo ""
echo "만약 /etc/exports가 없거나 잘못되었다면:"
echo "  sudo bash -c 'echo \"/nfsroot 192.168.10.0/24(rw,sync,no_subtree_check,no_root_squash)\" > /etc/exports'"
echo "  sudo exportfs -ra"
echo "  sudo systemctl restart nfs-kernel-server"
echo ""
echo "보드에서 마운트가 안되어 있다면:"
echo "  (각 보드에서) mount -t nfs 192.168.10.2:/nfsroot /mnt/nfs"
echo ""
