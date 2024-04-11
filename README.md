# MyNodeAddon

1.读取 json，构造 IR 结构体类型，将结构体名称和类型保存到 map 中
2.根据生产者生产的结构体类型，从 map 中获取对应的 IR 结构体类型
3.根据 IR 结构体类型，构造 nodejs 对象