#ifndef SN100_SAMPLE_QC_SHELL_H
#define SN100_SAMPLE_QC_SHELL_H

class QObject;
class SampleStore;
class QcStore;
class SamplePage;

// 在GUI线程、两个Store移入worker后、初始化前接一次；不在此处理SQL或修改草稿。
bool connectSampleQuality(SampleStore *samples, QcStore *quality,
                          SamplePage *samplePage, QObject *context);

#endif
