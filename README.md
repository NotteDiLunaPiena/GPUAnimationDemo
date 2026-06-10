# GPU Animation Demo

## 概要

DirectX11、C++、HLSLを用いて制作したGPUアニメーション最適化デモです。

CPUで実行していたスキニング処理をGPUへ移行し、大量のキャラクターを効率的に描画することを目的として開発しました。

また、Compute ShaderとVertex ShaderによるGPUスキニングの比較、アニメーションベイク、フラスタムカリングなどを実装し、それぞれの処理負荷やパフォーマンスの違いを可視化できるようにしています。

---

## 動画

【作品動画URL】
https://drive.google.com/file/d/1bW8DOSOcc-T0ENa3GiGIEUhziSRub3fh/view?usp=drive_link

---

## スクリーンショット

### GPU Animation Demo




### パフォーマンス比較

<img width="1280" height="720" alt="説明" src="https://github.com/user-attachments/assets/e894fd04-3ff7-4bff-9f35-25ec20b297ab" />

---

## 実装機能

### Animation System

* FBXアニメーション再生
* ボーン行列更新
* アニメーション切り替え

### GPU Skinning

* Vertex Shader Skinning
* Compute Shader Skinning
* CPU Skinningとの比較

### Animation Bake

* アニメーションデータを事前計算
* 実行時負荷の削減

### Instance Rendering

* Instancingによる大量描画

### Frustum Culling

* 画面外オブジェクトの描画除外
* 描画負荷軽減

### Debug UI

* インスタンス数変更
* スキニング方式切り替え
* カリングON/OFF
* ベイクON/OFF

---

## 使用技術

### 言語

* C++
* HLSL

### API

* DirectX11

### ライブラリ

* Assimp
* ImGui
* DirectXTK

### 開発環境

* Visual Studio 2022

---

## 制作背景

専門学校でゲーム開発を学ぶ中で、大量のキャラクターを描画する際のCPU負荷に興味を持ちました。

そこでCPUで行われるスキニング処理をGPUへ移行し、どの程度パフォーマンスが改善されるのかを検証するため、本作品を制作しました。

また、単にGPUスキニングを実装するだけでなく、複数の手法を比較できる環境を構築することで、それぞれの特徴や用途を理解できる構成を目指しました。

---

## 苦労した点

最も苦労したのは、CPUからGPUへのボーン行列やアニメーションデータの受け渡しです。

実装当初はモデルの頂点が崩れたり、アニメーションが正常に再生されない問題が発生しました。

原因特定のためにログ出力やデバッグ機能を利用し、CPU側とGPU側のデータを比較しながら検証を行いました。

その結果、データ構造や受け渡し順序の問題を特定し、GPUスキニングを正常に動作させることができました。

---

## 学んだこと

* GPUスキニングの仕組み
* Compute Shaderの活用方法
* CPUとGPUの役割分担
* 描画最適化手法
* デバッグによる問題切り分け

---

## 今後の改善

* DirectX12対応
* Animation Blend
* LOD対応
* Occlusion Culling
* マルチスレッド対応

---

## 作者

HAL東京 ゲーム制作学科

才田 智弥
