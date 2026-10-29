"""
LeNet-5 网络定义，与 lenet_mnist_model.pth 的 state_dict 严格对应。

checkpoint 结构（已实测）：
    conv1.weight (10, 1, 5, 5)   conv1.bias (10,)
    conv2.weight (20, 10, 5, 5)  conv2.bias (20,)
    fc1.weight   (50, 320)       fc1.bias   (50,)
    fc2.weight   (10, 50)        fc2.bias   (10,)

输入 28x28 -> conv1(5x5) 24x24 -> pool 12x12
           -> conv2(5x5) 8x8   -> pool 4x4 -> flatten 320
           -> fc1 50 -> fc2 10

checkpoint 里没有 BatchNorm / Dropout，所以网络结构是确定的；
不确定的只有激活函数（ReLU / Tanh）和池化方式（Max / Avg），
这两个由 test_lenet.py 的 sweep 自动判定。
"""

import torch.nn as nn


class LeNet5(nn.Module):
    """经典 LeNet-5（PyTorch 教程版本）。

    Args:
        activation: "relu" 或 "tanh"，作用于 conv1/conv2/fc1 之后。
        pool:       "max" 或 "avg"，两个 2x2 下采样层。
    """

    def __init__(self, activation: str = "relu", pool: str = "max"):
        super().__init__()

        if activation not in ("relu", "tanh"):
            raise ValueError(f"activation 必须是 relu/tanh，收到 {activation!r}")
        if pool not in ("max", "avg"):
            raise ValueError(f"pool 必须是 max/avg，收到 {pool!r}")

        self.conv1 = nn.Conv2d(1, 10, kernel_size=5)
        self.conv2 = nn.Conv2d(10, 20, kernel_size=5)
        self.fc1 = nn.Linear(320, 50)
        self.fc2 = nn.Linear(50, 10)

        self.act = nn.ReLU() if activation == "relu" else nn.Tanh()
        self.pool = nn.MaxPool2d(2, 2) if pool == "max" else nn.AvgPool2d(2, 2)

        self.activation_name = activation
        self.pool_name = pool

    def forward(self, x):
        x = self.pool(self.act(self.conv1(x)))   # 28 -> 24 -> 12
        x = self.pool(self.act(self.conv2(x)))   # 12 -> 8  -> 4
        x = x.flatten(1)                          # 20*4*4 = 320
        x = self.act(self.fc1(x))
        return self.fc2(x)
