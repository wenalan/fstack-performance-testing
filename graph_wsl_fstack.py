import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.ticker import FuncFormatter

def us_to_ms_formatter(x, pos):
    return f'{x/1000:.0f}'

def create_performance_charts():
    # 假设你有多个包大小的数据
    packet_sizes = [64, 128, 256, 512, 1024, 2048, 4096, 8192]
    
    # 示例数据
    avg_latency = [2333, 2061, 2499, 2446, 2533, 5734, 5238, 3570]  # us
    p50_latency = [2399, 2356, 2406, 2385, 2397, 2531, 2570, 2514]  # us
    p90_latency = [2745, 2566, 2942, 2739, 3018, 15611, 10992, 3802]  # us
    p99_latency = [4933, 3962, 4866, 3644, 4442, 43900, 44566, 31269]  # us
    min_latency = [1090, 1114, 1134, 1171, 1364, 1469, 1490, 1620]  # us
    max_latency = [18461, 8205, 10077, 31310, 25163, 59511, 88602, 48359]  # us
    throughput = [428, 485, 400, 408, 394, 174, 190, 280]  # requests/sec
    
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    
    # 子图1
    ax1 = axes[0, 0]
    ax1.plot(packet_sizes, avg_latency, 'b-o', label='avg lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, p50_latency, 'm-*', label='P50 lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, p90_latency, 'c-p', label='P90 lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, p99_latency, 'r-^', label='P99 lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, min_latency, 'y-<', label='min lat', linewidth=2, markersize=8, alpha=0.6)
    ax1.plot(packet_sizes, max_latency, 'k->', label='max lat', linewidth=2, markersize=8, alpha=0.6)
    ax1.set_xlabel('size (bytes)')
    ax1.set_ylabel('latency (ms)')
    ax1.set_title('latency vs size')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    ax1.set_xscale('log', base=2)
    x_ticks = [64, 128, 256, 512, 1024, 2048, 4096, 8192]
    ax1.set_xticks(x_ticks)
    x_tick_labels = ['64', '128', '256', '512', '1024', '2048', '4096', '8192']
    ax1.set_xticklabels(x_tick_labels, rotation=45, fontsize=9)

    # 应用formatter
    ax1.yaxis.set_major_formatter(FuncFormatter(us_to_ms_formatter))

    # 子图2
    ax2 = axes[0, 1]
    ax2.plot(packet_sizes, throughput, 'm-D', linewidth=2, markersize=8)
    ax2.set_xlabel('size (bytes)')
    ax2.set_ylabel('throughput (req/s)')
    ax2.set_title('throughput vs size')
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log', base=2)
    x_ticks = [64, 128, 256, 512, 1024, 2048, 4096, 8192]
    ax2.set_xticks(x_ticks)
    x_tick_labels = ['64', '128', '256', '512', '1024', '2048', '4096', '8192']
    ax2.set_xticklabels(x_tick_labels, rotation=45, fontsize=9)
    
    # 子图3
    np.random.seed(42)

    ax3 = axes[1, 0]
    latency_data = []
    for i, size in enumerate(packet_sizes):
        data = np.random.lognormal(np.log(p90_latency[i]), 0.5, 1000)
        latency_data.append(data)
    
    bp = ax3.boxplot(latency_data, positions=range(len(packet_sizes)), 
                     widths=0.6, patch_artist=True)
    
    for box in bp['boxes']:
        box.set(facecolor='lightblue', alpha=0.7)
    for whisker in bp['whiskers']:
        whisker.set(color='gray', linewidth=1.5)
    for cap in bp['caps']:
        cap.set(color='gray', linewidth=1.5)
    for median in bp['medians']:
        median.set(color='red', linewidth=2)
    
    ax3.set_xlabel('size (bytes)')
    ax3.set_ylabel('p90 latency (ms)')
    ax3.set_title('p90 latency distribute')
    ax3.set_xticks(range(len(packet_sizes)))
    ax3.set_xticklabels(packet_sizes)
    ax3.set_xticklabels(packet_sizes, rotation=45, fontsize=9)
    ax3.grid(True, alpha=0.3, axis='y')
    ax3.yaxis.set_major_formatter(FuncFormatter(us_to_ms_formatter))
    
    # 子图4
    ax4 = axes[1, 1]
    scatter = ax4.scatter(throughput, p90_latency, s=[size/10 for size in packet_sizes], 
                          c=packet_sizes, cmap='viridis', alpha=0.7, edgecolors='black')
    
    for i, size in enumerate(packet_sizes):
        ax4.annotate(f'{size}B', (throughput[i], p90_latency[i]), 
                    xytext=(5, 5), textcoords='offset points', fontsize=9)
    
    ax4.set_xlabel('throughput (req/s)')
    ax4.set_ylabel('p90 latency (ms)')
    ax4.set_title('p90 latency - throughput')
    ax4.grid(True, alpha=0.3)
    ax4.yaxis.set_major_formatter(FuncFormatter(us_to_ms_formatter))
    
    plt.colorbar(scatter, ax=ax4, label='size (bytes)')
    plt.tight_layout()
    
    # 保存文件
    plt.savefig('performance_summary.png', dpi=300, bbox_inches='tight')
    print(f"saved performance_summary.png")
    
    # 显示图形
    #plt.show()

# 调用函数
if __name__ == "__main__":
    create_performance_charts()
