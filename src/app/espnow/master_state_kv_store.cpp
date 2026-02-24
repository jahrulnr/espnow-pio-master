#include "master_state_kv_store.h"

#include "payload_codec.h"

#include <LittleFS.h>
#include <esp_log.h>
#include <vector>

namespace app::espnow::state_store {

namespace {

static constexpr const char* TAG = "state_kv_store";
static constexpr const char* STORE_DIR = "/data";
static constexpr const char* STORE_PATH = "/data/state_latest.csv";

struct Row {
  String state;
  String key;
  String value;
};

struct Field {
  String key;
  String value;
};

struct StateTimestamp {
  bool used = false;
  String state;
  uint32_t lastUpdateMs = 0;
};

static constexpr size_t kStateTimestampSlots = 16;
StateTimestamp stateTimestamps[kStateTimestampSlots];

String csvEscape(const String& value) {
  String out = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    if (ch == '"') {
      out += "\"\"";
    } else {
      out += ch;
    }
  }
  out += "\"";
  return out;
}

bool parseCsvLine(const String& line, Row& rowOut) {
  std::vector<String> columns;
  String current;
  bool inQuotes = false;

  for (size_t i = 0; i < line.length(); ++i) {
    const char ch = line[i];
    if (ch == '"') {
      if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
        current += '"';
        ++i;
      } else {
        inQuotes = !inQuotes;
      }
      continue;
    }

    if (ch == ',' && !inQuotes) {
      columns.push_back(current);
      current = "";
      continue;
    }

    current += ch;
  }
  columns.push_back(current);

  if (columns.size() != 3) {
    return false;
  }

  rowOut.state = columns[0];
  rowOut.key = columns[1];
  rowOut.value = columns[2];
  return true;
}

bool loadRows(std::vector<Row>& rowsOut) {
  rowsOut.clear();
  if (!LittleFS.exists(STORE_PATH)) {
    return true;
  }

  File file = LittleFS.open(STORE_PATH, "r");
  if (!file) {
    ESP_LOGW(TAG, "Failed opening store for read");
    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line == "state,key,value") {
      continue;
    }

    Row row;
    if (parseCsvLine(line, row)) {
      rowsOut.push_back(row);
    }
  }

  file.close();
  return true;
}

bool saveRows(const std::vector<Row>& rows) {
  if (!LittleFS.exists(STORE_DIR)) {
    LittleFS.mkdir(STORE_DIR);
  }

  File file = LittleFS.open(STORE_PATH, "w");
  if (!file) {
    ESP_LOGW(TAG, "Failed opening store for write");
    return false;
  }

  file.println("state,key,value");
  for (const auto& row : rows) {
    file.print(csvEscape(row.state));
    file.print(',');
    file.print(csvEscape(row.key));
    file.print(',');
    file.println(csvEscape(row.value));
  }

  file.close();
  return true;
}

void parseFields(const String& payload, std::vector<Field>& fieldsOut) {
  fieldsOut.clear();
  int start = 0;
  while (start < static_cast<int>(payload.length())) {
    const int end = payload.indexOf(app::espnow::codec::kSeparator, start);
    const int tokenEnd = end < 0 ? payload.length() : end;
    String token = payload.substring(start, tokenEnd);
    token.trim();

    const int equalsPos = token.indexOf('=');
    if (equalsPos > 0) {
      Field field;
      field.key = token.substring(0, equalsPos);
      field.value = token.substring(equalsPos + 1);
      field.key.trim();
      field.value.trim();
      if (!field.key.isEmpty()) {
        fieldsOut.push_back(field);
      }
    }

    if (end < 0) {
      break;
    }
    start = end + strlen(app::espnow::codec::kSeparator);
  }
}

bool isTruthySuccess(const String& value) {
  String normalized = value;
  normalized.toLowerCase();
  return normalized == "1" || normalized == "true" || normalized == "ok" || normalized == "success";
}

bool shouldSkipUpdateByStatus(const std::vector<Field>& fields) {
  for (const auto& field : fields) {
    if (field.key == "ok" || field.key == "status") {
      if (!isTruthySuccess(field.value)) {
        return true;
      }
    }
  }
  return false;
}

void touchStateTimestamp(const String& stateName, uint32_t nowMs) {
  for (auto& slot : stateTimestamps) {
    if (!slot.used) {
      continue;
    }
    if (slot.state == stateName) {
      slot.lastUpdateMs = nowMs;
      return;
    }
  }

  for (auto& slot : stateTimestamps) {
    if (slot.used) {
      continue;
    }
    slot.used = true;
    slot.state = stateName;
    slot.lastUpdateMs = nowMs;
    return;
  }

  stateTimestamps[0].used = true;
  stateTimestamps[0].state = stateName;
  stateTimestamps[0].lastUpdateMs = nowMs;
}

}  // namespace

bool upsertFromStatePayload(const String& payload) {
  if (payload.isEmpty()) {
    return false;
  }

  std::vector<Field> fields;
  parseFields(payload, fields);
  if (fields.empty()) {
    return false;
  }

  String stateName;
  for (const auto& field : fields) {
    if (field.key == "state") {
      stateName = field.value;
      break;
    }
  }

  if (stateName.isEmpty()) {
    return false;
  }

  if (shouldSkipUpdateByStatus(fields)) {
    ESP_LOGI(TAG, "Skip upsert for state=%s due to failed ok/status", stateName.c_str());
    return true;
  }

  touchStateTimestamp(stateName, millis());

  std::vector<Row> rows;
  if (!loadRows(rows)) {
    return false;
  }

  bool changed = false;
  for (const auto& field : fields) {
    if (field.key == "state") {
      continue;
    }

    bool found = false;
    for (auto& row : rows) {
      if (row.state == stateName && row.key == field.key) {
        if (row.value != field.value) {
          row.value = field.value;
          changed = true;
        }
        found = true;
        break;
      }
    }

    if (!found) {
      rows.push_back(Row{stateName, field.key, field.value});
      changed = true;
    }
  }

  if (!changed) {
    return true;
  }

  const bool saved = saveRows(rows);
  if (saved) {
    ESP_LOGD(TAG, "Upserted latest state values for %s", stateName.c_str());
  }
  return saved;
}

bool getLatestValue(const String& state, const String& key, String& valueOut) {
  valueOut = "";
  if (state.isEmpty() || key.isEmpty()) {
    return false;
  }

  std::vector<Row> rows;
  if (!loadRows(rows)) {
    return false;
  }

  for (const auto& row : rows) {
    if (row.state == state && row.key == key) {
      valueOut = row.value;
      return true;
    }
  }

  return false;
}

bool getLastUpdateMs(const String& state, uint32_t& lastUpdateMsOut) {
  lastUpdateMsOut = 0;
  if (state.isEmpty()) {
    return false;
  }

  for (const auto& slot : stateTimestamps) {
    if (!slot.used) {
      continue;
    }
    if (slot.state == state) {
      lastUpdateMsOut = slot.lastUpdateMs;
      return true;
    }
  }

  return false;
}

}  // namespace app::espnow::state_store
