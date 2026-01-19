import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
from matplotlib.ticker import FuncFormatter

def us_to_ms_formatter(x, pos):
    return f'{x/1000:.0f}'

def set_auto_log_scale(ax, data_list, margin_factor=0.1):
    all_data = np.concatenate([d for d in data_list if len(d) > 0])
    
    if len(all_data) == 0:
        ax.set_yscale('log')
        ax.set_ylim([0.1, 1000])
        ax.set_yticks([0.1, 1, 10, 100, 1000])
        return
    
    all_data = all_data[np.isfinite(all_data)]
    all_data = all_data[all_data > 0]
    
    if len(all_data) == 0:
        ax.set_yscale('log')
        ax.set_ylim([0.1, 1000])
        ax.set_yticks([0.1, 1, 10, 100, 1000])
        return
    
    y_min = np.min(all_data)
    y_max = np.max(all_data)
    
    if y_min > 0:
        min_exp = np.floor(np.log10(y_min))
    else:
        min_exp = -1
        
    if y_max > 0:
        max_exp = np.ceil(np.log10(y_max))
    else:
        max_exp = 2
    
    min_exp -= margin_factor
    max_exp += margin_factor
    
    log_y_min = 10 ** min_exp
    log_y_max = 10 ** max_exp
    
    log_y_min = max(0.001, log_y_min)
    log_y_max = min(1e9, log_y_max)
    
    ax.set_yscale('log')
    ax.set_ylim([log_y_min, log_y_max])
    
    ticks = []
    for exp in range(int(min_exp), int(max_exp) + 1):
        base = 10 ** exp
        ticks.extend([base, 2*base, 5*base])
    
    ticks = [t for t in ticks if log_y_min <= t <= log_y_max]
    if ticks:
        ax.set_yticks(ticks)
        
        def format_label(x):
            if x < 0.01:
                return f'{x:.3f}'
            elif x < 0.1:
                return f'{x:.2f}'
            elif x < 1:
                return f'{x:.1f}'
            elif x < 10:
                return f'{x:.0f}'
            elif x < 1000:
                return f'{int(x)}'
            elif x < 1000000:
                return f'{int(x/1000)}K'
            elif x < 1000000000:
                return f'{int(x/1000000)}M'
            else:
                return f'{int(x/1000000000)}G'
        
        ax.set_yticklabels([format_label(t) for t in ticks])

def create_performance_charts(report_name: str):
    base_dir = Path(__file__).resolve().parent
    output_dir = base_dir / "output"
    output_dir.mkdir(parents=True, exist_ok=True)

    summary_path = output_dir / f"{report_name}_sum.csv"
    if not summary_path.exists():
        raise FileNotFoundError(f"CSV file not found: {summary_path}")

    df = pd.read_csv(summary_path).sort_values("payload_size")

    packet_sizes = df["payload_size"].tolist()
    avg_latency = (df["avg_latency_ns"] / 1000.0).tolist()  # us
    p50_latency = (df["p50_ns"] / 1000.0).tolist()  # us
    p90_latency = (df["p90_ns"] / 1000.0).tolist()  # us
    p99_latency = (df["p99_ns"] / 1000.0).tolist()  # us
    min_latency = (df["min_latency_ns"] / 1000.0).tolist()  # us
    max_latency = (df["max_latency_ns"] / 1000.0).tolist()  # us
    throughput = df["throughput_rps"].tolist()  # requests/sec
    
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    
    context = f"[{report_name}]"

    # Subplot 1
    ax1 = axes[0, 0]
    #ax1.plot(packet_sizes, avg_latency, 'b-o', label='avg lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, p50_latency, 'm-*', label='P50 lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, p90_latency, 'c-p', label='P90 lat', linewidth=2, markersize=8)
    #ax1.plot(packet_sizes, p99_latency, 'r-^', label='P99 lat', linewidth=2, markersize=8)
    ax1.plot(packet_sizes, min_latency, 'y-<', label='min lat', linewidth=2, markersize=8, alpha=0.6)
    #ax1.plot(packet_sizes, max_latency, 'k->', label='max lat', linewidth=2, markersize=8, alpha=0.6)
    ax1.set_xlabel('size (bytes)')
    ax1.set_ylabel('latency (us)')
    ax1.set_title(f'{context} latency vs size')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    ax1.set_xscale('log', base=2)
    x_ticks = [64, 128, 256, 512, 1024, 2048, 4096, 8192]
    ax1.set_xticks(x_ticks)
    x_tick_labels = ['64', '128', '256', '512', '1024', '2048', '4096', '8192']
    ax1.set_xticklabels(x_tick_labels, rotation=45, fontsize=9)

    # Apply formatter
    #ax1.yaxis.set_major_formatter(FuncFormatter(us_to_ms_formatter))

    # Subplot 2
    ax2 = axes[0, 1]
    ax2.plot(packet_sizes, throughput, 'm-D', linewidth=2, markersize=8)
    ax2.set_xlabel('size (bytes)')
    ax2.set_ylabel('throughput (req/s)')
    ax2.set_title(f'{context} throughput vs size')
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log', base=2)
    x_ticks = [64, 128, 256, 512, 1024, 2048, 4096, 8192]
    ax2.set_xticks(x_ticks)
    x_tick_labels = ['64', '128', '256', '512', '1024', '2048', '4096', '8192']
    ax2.set_xticklabels(x_tick_labels, rotation=45, fontsize=9)
    
    # Subplot 3
    ax3 = axes[1, 0]
    latency_data = []

    for size in packet_sizes:
        detail_path = output_dir / f"{report_name}_{size}.csv"
        if not detail_path.exists():
            raise FileNotFoundError(f"Latency detail CSV not found: {detail_path}")
        detail_df = pd.read_csv(detail_path)
        if detail_df.empty:
            raise ValueError(f"No latency samples in {detail_path}")
        if "latency_ns" in detail_df.columns:
            latencies = detail_df["latency_ns"]
        else:
            latencies = detail_df.iloc[:, 0]
        latency_data.append((latencies / 1000.0).to_numpy())  # us

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
    ax3.set_ylabel('latency (us)')
    ax3.set_title(f'{context} latency distribution')
    ax3.set_xticks(range(len(packet_sizes)))
    ax3.set_xticklabels(packet_sizes, rotation=45, fontsize=9)
    ax3.set_yscale('log')
    #ax3.set_ylim([1000, 50000])
    #ax3.set_yticks([1000, 2000, 3000, 5000, 7000, 10000, 
    #       15000, 20000, 30000, 50000])
    #ax3.set_yticklabels(['1K', '2K', '3K', '5K', '7K', '10K', 
    #                 '15K', '20K', '30K', '50K'])
    set_auto_log_scale(ax3, latency_data, margin_factor=0.1)
    ax3.grid(True, alpha=0.3, axis='y')
    
    # Subplot 4
    ax4 = axes[1, 1]
    scatter = ax4.scatter(throughput, p90_latency, s=[size/10 for size in packet_sizes], 
                          c=packet_sizes, cmap='viridis', alpha=0.7, edgecolors='black')
    
    for i, size in enumerate(packet_sizes):
        ax4.annotate(f'{size}B', (throughput[i], p90_latency[i]), 
                    xytext=(5, 5), textcoords='offset points', fontsize=9)
    
    ax4.set_xlabel('throughput (req/s)')
    ax4.set_ylabel('p90 latency (us)')
    ax4.set_title(f'{context} p90 latency - throughput')
    ax4.grid(True, alpha=0.3)
    #ax4.yaxis.set_major_formatter(FuncFormatter(us_to_ms_formatter))
    
    plt.colorbar(scatter, ax=ax4, label='size (bytes)')
    plt.tight_layout()
    
    # Save figure
    output_path = base_dir / "output" / f"{report_name}.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"saved {output_path}")
    
    # Show chart
    # plt.show()

# Entry point
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate latency/throughput charts")
    parser.add_argument(
        "report_name",
        nargs="?",
        default="performance_summary",
        help="base name used for CSV input, PNG output, and chart titles",
    )
    args = parser.parse_args()
    create_performance_charts(args.report_name)
