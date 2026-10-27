#!/usr/bin/env python3
"""Private host-scroll observer; original formal cases/sources remain unchanged."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import sys
import xml.etree.ElementTree as ET

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
PROGRAM=ROOT/'06_programs/sn100_host'
HELPERS=ROOT/'temp/sn100_host/offline_followup_20261026/samples_reset'
sys.path.insert(0,str(HELPERS))
from run_reset_slice import require,run,save,sha
from run_workflow import inventory
from run_verification_v2 import environment

METHOD='smallViewportScrollAndApply'
INCLUDES='''// VIEWPORT_OBSERVER_BEGIN
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
// VIEWPORT_OBSERVER_END
'''
CASE='''    // VIEWPORT_OBSERVER_BEGIN
    void smallViewportScrollAndApply()
    {
        QScrollArea host;
        host.setFrameShape(QFrame::NoFrame);
        host.setWidgetResizable(false);
        auto *page = new LanguageSettingPage;
        host.setWidget(page);
        host.resize(900, 600);
        host.move(20, 20);
        host.show();
        QTRY_VERIFY(page->isVisible());
        auto *apply = applyButton(page);
        QVERIFY(apply);
        host.ensureWidgetVisible(apply, 12, 12);
        QTRY_VERIFY(host.horizontalScrollBar()->value() > 0 && host.verticalScrollBar()->value() > 0);
        const QRect buttonRect(apply->mapTo(host.viewport(), QPoint()), apply->size());
        QVERIFY(host.viewport()->rect().contains(buttonRect));
        QCOMPARE(page->size(), QSize(1451, 853));
        bool nativeHit = false;
        if (QGuiApplication::platformName() == QStringLiteral("xcb"))
        {
            host.raise();
            host.activateWindow();
            QVERIFY(QTest::qWaitForWindowExposed(&host));
            QWidget *hit = QApplication::widgetAt(apply->mapToGlobal(apply->rect().center()));
            nativeHit = hit == apply || (hit && apply->isAncestorOf(hit));
            QVERIFY(nativeHit);
        }
        const QString evidence = qEnvironmentVariable("SN100_CN_VIEWPORT_EVIDENCE");
        QVERIFY(QDir::isAbsolutePath(evidence) && QFileInfo(evidence).isDir());
        QVERIFY(host.grab().save(QDir(evidence).filePath(QStringLiteral("scrolled_page.png"))));
        QTest::mouseClick(apply, Qt::LeftButton);
        QPointer<CusMessageBox> message = feedback(page);
        QTRY_VERIFY(message && message->isReadyForInput());
        QCOMPARE(message->getMsg(), unsupported);
        QCOMPARE(page->property("lastStatus").toString(), unsupported);
        QVERIFY(message->grab().save(QDir(evidence).filePath(QStringLiteral("unavailable_warning.png"))));
        QJsonObject observed{{"host_width",host.width()},{"host_height",host.height()},
            {"canvas_width",page->width()},{"canvas_height",page->height()},
            {"viewport_width",host.viewport()->width()},{"viewport_height",host.viewport()->height()},
            {"scroll_x",host.horizontalScrollBar()->value()},{"scroll_y",host.verticalScrollBar()->value()},
            {"button_fully_in_viewport",host.viewport()->rect().contains(buttonRect)},
            {"xcb_native_widgetAt_hit",nativeHit},{"qpa",QGuiApplication::platformName()},
            {"lastStatus",page->property("lastStatus").toString()},
            {"capture","Actual QWidget::grab; no crop, scale, overlay or original-form changes"}};
        page->dismissFeedback();
        QTRY_VERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
        observed.insert("message_destroyed_and_modal_cleared",true);
        QVERIFY(host.close());
        QFile record(QDir(evidence).filePath(QStringLiteral("observed.json")));
        QVERIFY(record.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        const QByteArray bytes=QJsonDocument(observed).toJson(QJsonDocument::Indented);
        QCOMPARE(record.write(bytes),qint64(bytes.size()));
    }
    // VIEWPORT_OBSERVER_END
'''


def flags(path):
    text=path.read_text(encoding='utf-8').replace('\\\n',' ')
    values=dict(re.findall(r'^([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.*)$',text,re.M))
    def words(name):
        value=values[name]
        for _ in range(10):
            if '$(' not in value:break
            def expand(m):
                require(m[1] in values or m[1]=='SUBLIBS','Unknown Makefile flag '+m[1])
                return values.get(m[1],'')
            value=re.sub(r'\$\(([A-Za-z_][A-Za-z_0-9]*)\)',expand,value)
        require('$(' not in value,'Unresolved flags')
        return shlex.split(value)
    return {name:words(name) for name in ('DEFINES','CXXFLAGS','INCPATH','LFLAGS','LIBS','OBJECTS')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go',action='store_true',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    require(args.root_go and os.geteuid()!=0 and os.environ.get('WSL_DISTRO_NAME')=='Ubuntu-24.04','Root slot and ordinary WSL required')
    require(args.output.is_absolute() and args.output.parent.resolve()==HERE and not args.output.exists(),'Fresh observer attempt required')
    original=HERE/'verification_02/build'
    formal_report=json.loads((HERE/'verification_02/verification.json').read_text())
    require(formal_report['passed'] and formal_report['all_owned_groups_gone'],'Formal two-backend14/0 required first')
    before=inventory(original,original/'language_setting_page_test')
    require(before==json.loads((HERE/'verification_02/build_after_tests.json').read_text()),'Original formal artifacts changed')
    frozen=json.loads((HERE/'verification_02/source_before.json').read_text())
    def inputs():
        observed={name:sha(PROGRAM/name) for name in frozen}
        require(observed==frozen,'Formal dependency changed')
        return observed
    inputs()
    args.output.mkdir(mode=0o700)
    source_dir,build,logs=(args.output/name for name in ('source','build','logs'))
    for path in (source_dir,build,logs,build/'.moc'):path.mkdir()
    original_source=(PROGRAM/'tests/language_setting_page_test.cpp').read_text(encoding='utf-8')
    source=original_source.replace('#include <QApplication>\n','#include <QApplication>\n'+INCLUDES,1)
    marker='\nprivate:\n    QString root_;'
    require(source.count(marker)==1,'Original test class marker ambiguous')
    source=source.replace(marker,'\n'+CASE+marker,1)
    stripped=re.sub(r'(?ms)^[ \t]*// VIEWPORT_OBSERVER_BEGIN\n.*?^[ \t]*// VIEWPORT_OBSERVER_END\n','',source)
    require(stripped==original_source,'Observer stripping does not restore exact original source')
    private_source=source_dir/'language_setting_page_test.cpp'
    private_source.write_text(source,encoding='utf-8')
    parsed=flags(original/'Makefile')
    require(set(parsed['OBJECTS'])==set(before['objects_sha256']),'Original object order changed')
    replaced='.obj/language_setting_page_test.o'
    reused={name:digest for name,digest in before['objects_sha256'].items() if name!=replaced}
    require(len(reused)==7 and len(before['objects_sha256'])==8,'Expected seven unchanged production/support objects')
    save(args.output/'binding_before.json',{'formal_inputs':inputs(),'original_artifacts':before,'flags':parsed})
    report={'schema':'sn100-private-cn-viewport-observer-v1','passed':False,'phases':[],'tests':[],
        'formal_source_modified':False,'original_source_sha256':sha(PROGRAM/'tests/language_setting_page_test.cpp'),
        'private_source_sha256':sha(private_source),'strip_observer_restores_original':True,
        'reused_objects_sha256':reused,'original_regression_or_performance_reclassification':False}
    save(args.output/'verification.json',report)
    try:
        runtime=args.output/'compile_runtime';runtime.mkdir()
        env,meta=environment(runtime,'offscreen');os.environ.clear();os.environ.update(env)
        includes=['-I'+str(build/'.moc')]+[
            '-I'+str((original/word[2:]).resolve()) if word.startswith('-I') and not Path(word[2:]).is_absolute() else word
            for word in parsed['INCPATH']]
        commands=[('moc',['/usr/lib/qt5/bin/moc',*parsed['DEFINES'],'--include',str(original/'.moc/moc_predefs.h'),
                         *includes,str(private_source),'-o',str(build/'.moc/language_setting_page_test.moc')],30),
                  ('compile_observer',['/usr/bin/g++','-c',*parsed['CXXFLAGS'],*includes,str(private_source),'-o',str(build/'observer.o')],120)]
        for label,command,timeout in commands:report['phases'].append(run(label,command,build,logs,timeout))
        binary=build/'language_viewport_observer'
        objects=[str(build/'observer.o') if name==replaced else str(original/name) for name in parsed['OBJECTS']]
        report['phases'].append(run('link',['/usr/bin/g++',*parsed['LFLAGS'],'-o',str(binary),*objects,*parsed['LIBS']],build,logs,60))
        own={str(path):sha(path) for path in (private_source,binary,build/'observer.o',build/'.moc/language_setting_page_test.moc')}
        save(args.output/'private_artifacts_before.json',own)
        for qpa in ('offscreen','xcb'):
            case=args.output/qpa;case.mkdir()
            runtime=case/'runtime';runtime.mkdir()
            evidence=case/'evidence';evidence.mkdir()
            env,meta=environment(runtime,qpa);env['SN100_CN_VIEWPORT_EVIDENCE']=str(evidence)
            os.environ.clear();os.environ.update(env)
            save(case/'environment.json',dict(meta,evidence=str(evidence)))
            receipt=run('observe',[str(binary),METHOD,'-v1','-o',str(case/'qt.txt')+',txt',
                        '-o',str(case/'qt.xml')+',junitxml'],case,case,60)
            text=(case/'qt.txt').read_text()
            xml=ET.parse(case/'qt.xml').getroot();cases=list(xml.iter('testcase'))
            require(re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped',text)==[('3','0','0')]
                and len(cases)==3 and all(c.get('result')=='pass' for c in cases)
                and not any(n.tag in ('error','failure','skipped') for c in cases for n in c),'Viewport observer failure')
            report['tests'].append({'qpa':qpa,'actual_exit':receipt['actual_exit'],'qt_totals':[3,0,0],
                'observed':json.loads((evidence/'observed.json').read_text()),
                'PNG_sha256':{p.name:sha(p) for p in evidence.glob('*.png')},'junit_attributes':xml.attrib})
        require(inventory(original,original/'language_setting_page_test')==before,'Original artifacts changed')
        require(all(sha(path)==digest for path,digest in own.items()),'Private artifacts changed')
        save(args.output/'binding_after.json',{'formal_inputs':inputs(),'original_artifacts':before,'flags':parsed})
        report['passed']=True
    finally:
        groups=[]
        for path in args.output.rglob('*.exit.json'):
            row=json.loads(path.read_text());pid=row.get('pid')
            if not pid:continue
            try:os.killpg(pid,0);gone=False
            except ProcessLookupError:gone=True
            groups.append({'pid':pid,'pgid':pid,'actual_exit':row['actual_exit'],'gone':gone})
        report['owned_groups']=groups;report['all_owned_groups_gone']=all(row['gone'] for row in groups)
        report['passed']=report['passed'] and report['all_owned_groups_gone']
        save(args.output/'verification.json',report)
    require(report['passed'],'Viewport observer failed; retain original attempt')
    print(json.dumps({'passed':True,'all_owned_groups_gone':True,'verification':str(args.output/'verification.json')}),flush=True)


if __name__=='__main__':main()
