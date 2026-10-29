#!/usr/bin/env python
"""
在 MNIST 测试集上评估 lenet_mnist_model.pth 的推理准确率。

用法（Windows / Anaconda）：
    set KMP_DUPLICATE_LIB_OK=TRUE
    D:\\Software\\anaconda3\\python.exe test_lenet.py

常用参数：
    --weights lenet_mnist_model.pth   模型路径
    --data-dir data                   MNIST 缓存目录（不存在会自动下载）
    --no-sweep                        跳过配方扫描，直接用 --activation/--pool/--norm
    --activation relu --pool max --norm mnist
    --out-dir reports                 报告输出目录

脚本会做三件事：
    1. 校验 checkpoint 的 key 与网络结构是否完全匹配；
    2. 扫描 (激活函数 x 池化方式 x 归一化) 共 8 种组合，定位训练时用的配方；
    3. 用最佳配方输出：总体准确率、逐类召回、混淆矩阵、置信度分布、
       并把混淆矩阵和“高置信度错判样本”保存成图片。
"""

import argparse
import json
import os
import sys
import time

# 本机 Anaconda 的 torch 与系统里另一个 OpenMP 运行库冲突，必须在 import torch 前设置
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")

import numpy as np
import torch

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from torchvision import datasets  # noqa: E402

from lenet import LeNet5  # noqa: E402

# 各归一化配方：(mean, std)；"none" 表示直接 ToTensor
NORMS = {
    "none": (0.0, 1.0),
    "mnist": (0.1307, 0.3081),
}

DIGITS = [str(i) for i in range(10)]


def load_checkpoint(path):
    """读取 state_dict，兼容整包保存（含 'state_dict' 键）的情况。"""
    obj = torch.load(path, map_location="cpu")

    if isinstance(obj, dict) and "state_dict" in obj and isinstance(obj["state_dict"], dict):
        obj = obj["state_dict"]

    if not isinstance(obj, dict):
        raise SystemExit(f"{path} 不是 state_dict（读到 {type(obj)}）")

    # 去掉可能存在的 "module." 前缀（DataParallel 保存的权重）
    return {k.replace("module.", "", 1): v for k, v in obj.items()}


def build_model(sd, activation, pool):
    model = LeNet5(activation=activation, pool=pool)
    missing, unexpected = model.load_state_dict(sd, strict=False)

    if missing or unexpected:
        raise SystemExit(
            "checkpoint 与网络结构不匹配\n"
            f"  缺少的 key: {missing}\n"
            f"  多余的 key: {unexpected}"
        )

    model.eval()
    return model


def load_mnist_test(data_dir):
    ds = datasets.MNIST(root=data_dir, train=False, download=True)
    images = ds.data.float().div(255.0).unsqueeze(1)  # (N,1,28,28)
    labels = ds.targets.clone().long()
    return images, labels


@torch.no_grad()
def evaluate(model, images, labels, norm, batch_size):
    mean, std = NORMS[norm]
    n = labels.numel()

    preds = torch.empty(n, dtype=torch.long)
    confs = torch.empty(n, dtype=torch.float32)

    for start in range(0, n, batch_size):
        stop = min(start + batch_size, n)
        x = images[start:stop]
        if norm != "none":
            x = (x - mean) / std

        probs = model(x).softmax(dim=1)
        conf, pred = probs.max(dim=1)

        preds[start:stop] = pred
        confs[start:stop] = conf

    return preds, confs


def accuracy(preds, labels):
    return (preds == labels).float().mean().item()


def print_sweep(results):
    print("\n配方扫描（8 种组合，MNIST 测试集 10000 张）")
    print("-" * 68)
    print(f"{'activation':<12}{'pool':<8}{'norm':<10}{'accuracy':>10}{'ms/img':>10}")
    print("-" * 68)
    for r in results:
        print(
            f"{r['activation']:<12}{r['pool']:<8}{r['norm']:<10}"
            f"{r['accuracy'] * 100:>9.2f}%{r['ms_per_image']:>10.3f}"
        )
    print("-" * 68)


def print_report(preds, labels, confs, config):
    n = labels.numel()
    correct_mask = preds == labels
    correct = int(correct_mask.sum())
    acc = correct / n

    print("\n" + "=" * 68)
    print("最佳配方：" f"activation={config['activation']}, pool={config['pool']}, norm={config['norm']}")
    print("=" * 68)
    print(f"测试样本      : {n}")
    print(f"预测正确      : {correct}")
    print(f"总体准确率    : {acc * 100:.2f}%")

    # 逐类召回
    labels_np = labels.numpy()
    preds_np = preds.numpy()
    confs_np = confs.numpy()

    print("\n逐类表现")
    print(f"{'digit':<7}{'support':>8}{'correct':>9}{'recall':>9}")
    recalls = []
    for d in range(10):
        mask = labels_np == d
        support = int(mask.sum())
        hit = int((preds_np[mask] == d).sum())
        recall = hit / support if support else 0.0
        recalls.append(recall)
        print(f"{d:<7}{support:>8}{hit:>9}{recall * 100:>8.2f}%")
    print(f"宏平均召回    : {np.mean(recalls) * 100:.2f}%")

    # 混淆矩阵
    cm = np.zeros((10, 10), dtype=np.int32)
    for t, p in zip(labels_np, preds_np):
        cm[t, p] += 1

    print("\n混淆矩阵（行=真实，列=预测）")
    header = "      " + "".join(f"{d:>7}" for d in DIGITS)
    print(header)
    for d in range(10):
        row = "".join(f"{int(cm[d, j]):>7}" for j in range(10))
        print(f"  {d}   {row}")

    # 置信度
    print("\n置信度（softmax 最大值）")
    print(f"正确样本均值  : {confs_np[correct_mask.numpy()].mean():.4f}")
    print(f"错误样本均值  : {confs_np[~correct_mask.numpy()].mean():.4f}")

    # 最容易混淆的类别对
    pairs = []
    for t in range(10):
        for p in range(10):
            if t != p and cm[t, p] > 0:
                pairs.append((int(cm[t, p]), t, p))
    pairs.sort(reverse=True)

    print("\n主要错判（真实 -> 预测 : 次数）")
    for cnt, t, p in pairs[:10]:
        print(f"  {t} -> {p} : {cnt}")

    return cm, acc, recalls, pairs


def save_confusion_matrix(cm, acc, config, out_dir):
    fig, ax = plt.subplots(figsize=(7.2, 6.0))
    im = ax.imshow(cm, cmap="Blues")

    ax.set_xticks(range(10), DIGITS)
    ax.set_yticks(range(10), DIGITS)
    ax.set_xlabel("Predicted")
    ax.set_ylabel("True")
    ax.set_title(
        f"MNIST test confusion matrix  acc={acc * 100:.2f}%  "
        f"({config['activation']}/{config['pool']}/{config['norm']})",
        fontsize=10,
    )

    thresh = cm.max() / 2.0
    for i in range(10):
        for j in range(10):
            if cm[i, j]:
                ax.text(
                    j, i, str(int(cm[i, j])), ha="center", va="center", fontsize=7,
                    color="white" if cm[i, j] > thresh else "black",
                )

    fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    fig.tight_layout()

    path = os.path.join(out_dir, "confusion_matrix.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def save_error_grid(images, labels, preds, confs, out_dir, count=16):
    wrong = (preds != labels).nonzero(as_tuple=False).flatten()
    if wrong.numel() == 0:
        return None

    # 挑“错了但很自信”的样本，比随机挑更有诊断价值
    order = torch.argsort(confs[wrong], descending=True)
    wrong = wrong[order][:count]

    cols = 4
    rows = (len(wrong) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(cols * 2.2, rows * 2.4))
    axes = np.atleast_1d(axes).ravel()

    for ax, idx in zip(axes, wrong.tolist()):
        ax.imshow(images[idx, 0].numpy(), cmap="gray")
        ax.set_title(
            f"true={int(labels[idx])} pred={int(preds[idx])}\nconf={float(confs[idx]):.3f}",
            fontsize=9,
        )
        ax.axis("off")

    for ax in axes[len(wrong):]:
        ax.axis("off")

    fig.suptitle("High-confidence errors", fontsize=11)
    fig.tight_layout()

    path = os.path.join(out_dir, "error_samples.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def main():
    parser = argparse.ArgumentParser(description="在 MNIST 测试集上评估 LeNet-5 checkpoint")
    parser.add_argument("--weights", default="lenet_mnist_model.pth", help="checkpoint 路径")
    parser.add_argument("--data-dir", default="data", help="MNIST 数据目录")
    parser.add_argument("--out-dir", default="reports", help="报告输出目录")
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--no-sweep", action="store_true", help="跳过配方扫描")
    parser.add_argument("--activation", default="relu", choices=["relu", "tanh"])
    parser.add_argument("--pool", default="max", choices=["max", "avg"])
    parser.add_argument("--norm", default="mnist", choices=list(NORMS.keys()))
    args = parser.parse_args()

    torch.set_num_threads(args.threads)
    torch.manual_seed(0)

    here = os.path.dirname(os.path.abspath(__file__))
    weights = args.weights if os.path.isabs(args.weights) else os.path.join(here, args.weights)
    data_dir = args.data_dir if os.path.isabs(args.data_dir) else os.path.join(here, args.data_dir)
    out_dir = args.out_dir if os.path.isabs(args.out_dir) else os.path.join(here, args.out_dir)
    os.makedirs(out_dir, exist_ok=True)

    if not os.path.isfile(weights):
        raise SystemExit(f"找不到模型文件: {weights}")

    print(f"torch        : {torch.__version__}")
    print(f"checkpoint   : {weights}")
    print(f"data dir     : {data_dir}")

    sd = load_checkpoint(weights)
    print("state_dict   : " + ", ".join(f"{k}{tuple(v.shape)}" for k, v in sd.items()))

    # 先用标准配方校验结构，不匹配会直接报错
    build_model(sd, "relu", "max")
    print("结构校验     : OK（key 与 shape 完全匹配）")

    images, labels = load_mnist_test(data_dir)
    print(f"测试集       : {labels.numel()} 张")

    if args.no_sweep:
        configs = [(args.activation, args.pool, args.norm)]
    else:
        configs = [
            (act, pool, norm)
            for act in ("relu", "tanh")
            for pool in ("max", "avg")
            for norm in ("mnist", "none")
        ]

    results = []
    for act, pool, norm in configs:
        model = build_model(sd, act, pool)

        t0 = time.perf_counter()
        preds, confs = evaluate(model, images, labels, norm, args.batch_size)
        elapsed = time.perf_counter() - t0

        results.append(
            {
                "activation": act,
                "pool": pool,
                "norm": norm,
                "accuracy": accuracy(preds, labels),
                "ms_per_image": elapsed * 1000.0 / labels.numel(),
                "preds": preds,
                "confs": confs,
            }
        )

    if len(results) > 1:
        print_sweep(sorted(results, key=lambda r: r["accuracy"], reverse=True))

    # 最佳配方（准确率最高；并列时优先 relu/max/mnist 这种最常见配方）
    preference = {("relu", "max", "mnist"): 3, ("relu", "max", "none"): 2}
    best = max(
        results,
        key=lambda r: (round(r["accuracy"], 6), preference.get((r["activation"], r["pool"], r["norm"]), 0)),
    )

    cm, acc, recalls, pairs = print_report(best["preds"], labels, best["confs"], best)

    cm_path = save_confusion_matrix(cm, acc, best, out_dir)
    err_path = save_error_grid(images, labels, best["preds"], best["confs"], out_dir)

    report = {
        "checkpoint": os.path.basename(weights),
        "test_samples": int(labels.numel()),
        "best_config": {k: best[k] for k in ("activation", "pool", "norm")},
        "accuracy": acc,
        "macro_recall": float(np.mean(recalls)),
        "per_class_recall": [float(r) for r in recalls],
        "per_class_support": np.bincount(labels.numpy(), minlength=10).tolist(),
        "confusion_matrix": cm.tolist(),
        "top_confusions": [{"true": t, "pred": p, "count": c} for c, t, p in pairs[:10]],
        "ms_per_image": best["ms_per_image"],
        "sweep": [
            {k: r[k] for k in ("activation", "pool", "norm", "accuracy", "ms_per_image")}
            for r in sorted(results, key=lambda r: r["accuracy"], reverse=True)
        ],
    }

    json_path = os.path.join(out_dir, "report.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)

    print("\n输出文件")
    print(f"  混淆矩阵 : {cm_path}")
    if err_path:
        print(f"  错判样本 : {err_path}")
    print(f"  JSON 报告: {json_path}")

    print("\n结论")
    print(f"  该 checkpoint 在 MNIST 测试集上的准确率为 {acc * 100:.2f}%")
    print(f"  判定训练配方：activation={best['activation']}, pool={best['pool']}, norm={best['norm']}")

    if acc < 0.9:
        print("  ! 准确率明显偏低，请确认模型不是只训练了少量 epoch，或输入预处理不匹配")
        sys.exit(1)


if __name__ == "__main__":
    main()
