// Enable with -DWITH_CHANNEL_MAPPING_TEST=ON. Checks survive Release builds.
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txsheet.h"
#include "toonz/txsheetexpr.h"
#include "texpression.h"
#include "tstream.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << "Failed at " << __LINE__ << ": " << #condition << '\n'; \
      return 1;                                                            \
    }                                                                      \
  } while (false)

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  TXsheet sheet;
  auto *object = sheet.getStageObject(TStageObjectId::ColumnId(0));
  const TChannelId height = object->createCustomChannel("height", 4.0);
  CHECK(height == TChannelId(50));
  auto *curve = object->findChannel(height);
  CHECK(curve && curve->getDefaultValue() == 4.0);
  curve->setValue(0, 4.0);
  curve->setValue(10, 8.0);
  CHECK(object->createCustomChannel("HEIGHT", 0) == TChannelIds::Invalid);
  for (const char *name : {"x", "ScaleX", "cell", "channel51", "1bad", "a.b", "a b", ""})
    CHECK(object->createCustomChannel(name, 0) == TChannelIds::Invalid);
  CHECK(object->createCustomChannel("bad", std::numeric_limits<double>::infinity()) ==
        TChannelIds::Invalid);

  TExpression expression;
  expression.setGrammar(sheet.getStageObjectTree()->getGrammar());
  for (const char *name : {"col1.height", "col1.channel50", "COL1.HEIGHT"}) {
    expression.setText(name);
    CHECK(expression.isValid());
    CHECK(expression.getCalculator()->compute(0, 1, 1) == 4.0);
    CHECK(dependsOn(expression, curve));
    QSet<int> columns;
    QSet<TDoubleParam *> params;
    referenceParams(expression, columns, params);
    CHECK(columns.contains(0) && params.contains(curve));
  }
  expression.setText("col1.height(11)");
  CHECK(expression.isValid());
  CHECK(expression.getCalculator()->compute(0, 1, 1) == 8.0);
  for (const char *name : {"col1.missing", "col1.channel49", "col1.channel4294967296"}) {
    expression.setText(name);
    CHECK(!expression.isValid());
  }
  const auto beforeRename = object->getCustomChannels();
  CHECK(object->renameCustomChannel(height, "buildingHeight"));
  CHECK(object->findChannel(height) == curve);
  for (const char *name : {"height", "buildingHeight", "channel50"})
    CHECK(object->findChannelId(name) == height);
  CHECK(object->createCustomChannel("height", 0) == TChannelIds::Invalid);
  expression.setText("col1.buildingHeight");
  CHECK(expression.isValid());
  CHECK(object->assignCustomChannels(beforeRename));
  CHECK(object->findChannelId("buildingHeight") == TChannelIds::Invalid);
  CHECK(object->renameCustomChannel(height, "buildingHeight"));

  std::unique_ptr<TStageObjectParams> snapshot(object->getParams());
  auto *copy = sheet.getStageObject(TStageObjectId::ColumnId(1));
  copy->assignParams(snapshot.get(), true);
  CHECK(copy->findChannel(height) != curve);
  CHECK(copy->findChannel(height)->getValue(10) == 8.0);
  copy->findChannel(height)->setValue(10, 16.0);
  CHECK(curve->getValue(10) == 8.0);

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const TFilePath path((temp.path() + "/channels.tnz").toStdWString());
  {
    TOStream os(path);
    // No exposed cells: the custom-only column must still be saved.
    sheet.getStageObjectTree()->saveData(os, 0, &sheet);
  }
  TXsheet restored;
  {
    TIStream is(path);
    is.setVersion(VersionNumber(1, 24));
    restored.getStageObjectTree()->loadData(is, &restored);
  }
  auto *loaded = restored.getStageObjectTree()->getStageObject(
      TStageObjectId::ColumnId(0), false);
  CHECK(loaded);
  CHECK(loaded->findChannelId("buildingHeight") == height);
  CHECK(loaded->findChannelId("height") == height);
  CHECK(loaded->findChannel(height)->getValue(10) == 8.0);
  expression.setGrammar(restored.getStageObjectTree()->getGrammar());
  expression.setText("col1.height(11)");
  CHECK(expression.isValid());
  CHECK(expression.getCalculator()->compute(0, 1, 1) == 8.0);
  CHECK(loaded->createCustomChannel("width", 2) == TChannelId(51));

  auto invalid = loaded->getCustomChannels();
  invalid.emplace(52, invalid.at(50));
  CHECK(!loaded->assignCustomChannels(invalid));
  CHECK(loaded->getCustomChannels().size() == 2);
  invalid = loaded->getCustomChannels();
  invalid.emplace(21, invalid.at(50));
  CHECK(!loaded->assignCustomChannels(invalid));
  // Exhaustion must fail instead of wrapping into historical IDs.
  TStageObject::CustomChannels last;
  last.emplace(UINT32_MAX, TStageObject::CustomChannel{
      "last", {"last"}, new TDoubleParam()});
  CHECK(copy->assignCustomChannels(last));
  CHECK(copy->createCustomChannel("overflow", 0) == TChannelIds::Invalid);
  return 0;
}
