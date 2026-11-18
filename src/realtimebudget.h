#pragma once
// PipeWire 的 module-rt 在每个客户端里按实时预算（RTTimeUSecMax：有门户问门户，没有问 rtkit）设 RLIMIT_RTTIME，
// 再让音频线程走实时调度。门户启动时没读到 rtkit 就会一直报 0（xdg-desktop-portal#2110）：软硬限制都成 0，
// 音频线程一转成实时，内核就 SIGKILL 整个进程，一出声就没了（#11）。
// 预算是 0 时设 DISABLE_RTKIT，让 PipeWire 不申请实时调度：歌照常放，音频线程按普通优先级跑。
// 要在 mpv 第一次连 PipeWire 之前调用。
namespace RealtimeBudget {
void refuseZero();
}
