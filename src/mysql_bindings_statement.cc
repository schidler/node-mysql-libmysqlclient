/*!
 * Copyright by Oleg Efimov and node-mysql-libmysqlclient contributors
 * See contributors list in README
 *
 * See license text in LICENSE file
 */

/*!
 * Include headers
 */
#include "./mysql_bindings_connection.h"
#include "./mysql_bindings_result.h"
#include "./mysql_bindings_statement.h"

/*!
 * Init V8 structures for MysqlStatement class
 */
Persistent<FunctionTemplate> MysqlStatement::constructor_template;

void MysqlStatement::Init(Handle<Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(MysqlStatement::New);

    // Constructor template
    constructor_template = Persistent<FunctionTemplate>::New(t);
    constructor_template->SetClassName(String::NewSymbol("MysqlStatement"));

    // Instance template
    Local<ObjectTemplate> instance_template = constructor_template->InstanceTemplate();
    instance_template->SetInternalFieldCount(1);

    // Constants
    NODE_DEFINE_CONSTANT(instance_template, STMT_ATTR_UPDATE_MAX_LENGTH);
    NODE_DEFINE_CONSTANT(instance_template, STMT_ATTR_CURSOR_TYPE);
    NODE_DEFINE_CONSTANT(instance_template, STMT_ATTR_PREFETCH_ROWS);

    // Properties
    instance_template->SetAccessor(V8STR("paramCount"), ParamCountGetter);

    // Methods
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "affectedRowsSync",   MysqlStatement::AffectedRowsSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "attrGetSync",        MysqlStatement::AttrGetSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "attrSetSync",        MysqlStatement::AttrSetSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "bindParamsSync",     MysqlStatement::BindParamsSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "closeSync",          MysqlStatement::CloseSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "dataSeekSync",       MysqlStatement::DataSeekSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "errnoSync",          MysqlStatement::ErrnoSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "errorSync",          MysqlStatement::ErrorSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "executeSync",        MysqlStatement::ExecuteSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "fetchAllSync",       MysqlStatement::FetchAllSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "fieldCountSync",     MysqlStatement::FieldCountSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "freeResultSync",     MysqlStatement::FreeResultSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "lastInsertIdSync",   MysqlStatement::LastInsertIdSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "numRowsSync",        MysqlStatement::NumRowsSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "prepareSync",        MysqlStatement::PrepareSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "resetSync",          MysqlStatement::ResetSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "resultMetadataSync", MysqlStatement::ResultMetadataSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "sendLongDataSync",   MysqlStatement::SendLongDataSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "storeResultSync",    MysqlStatement::StoreResultSync);
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "sqlStateSync",       MysqlStatement::SqlStateSync);

    // Make it visible in JavaScript
    target->Set(String::NewSymbol("MysqlStatement"), constructor_template->GetFunction());
}

MysqlStatement::MysqlStatement(MYSQL_STMT *my_stmt): ObjectWrap() {
    this->_stmt = my_stmt;
    this->binds = NULL;
    this->param_count = 0;
    this->prepared = false;
    this->stored = false;
}

MysqlStatement::~MysqlStatement() {
    if (this->_stmt) {
        if (this->prepared) {
            for (uint64_t i = 0; i < this->param_count; i++) {
                if (this->binds[i].buffer_type == MYSQL_TYPE_LONG) {
                    if (this->binds[i].is_unsigned) {
                        delete static_cast<unsigned int *>(this->binds[i].buffer); // NOLINT
                    } else {
                        delete static_cast<int *>(this->binds[i].buffer);
                    }
                } else if (this->binds[i].buffer_type == MYSQL_TYPE_DOUBLE) {
                    delete static_cast<double *>(this->binds[i].buffer);
                } else if (this->binds[i].buffer_type == MYSQL_TYPE_STRING) {
                    // TODO(Sannis): Or delete?
                    delete[] static_cast<char *>(this->binds[i].buffer);
                    delete static_cast<unsigned long *>(this->binds[i].length); // NOLINT
                } else if (this->binds[i].buffer_type == MYSQL_TYPE_DATETIME) {
                    delete static_cast<MYSQL_TIME *>(this->binds[i].buffer);
                } else {
                    printf("MysqlStatement::~MysqlStatement: o_0\n");
                }
            }
            delete[] this->binds;
        }
        mysql_stmt_free_result(this->_stmt);
        mysql_stmt_close(this->_stmt);
    }
}

/** internal
 * new MysqlStatement()
 *
 * Creates new MySQL prepared statement object
 **/
Handle<Value> MysqlStatement::New(const Arguments& args) {
    HandleScope scope;

    REQ_EXT_ARG(0, js_stmt);
    MYSQL_STMT *my_stmt = static_cast<MYSQL_STMT*>(js_stmt->Value());
    MysqlStatement *binding_stmt = new MysqlStatement(my_stmt);
    binding_stmt->Wrap(args.Holder());

    return args.Holder();
}

/** read-only
 * MysqlStatement#paramCount -> Integer
 *
 * Returns the number of parameter for the given statement
 **/
Handle<Value> MysqlStatement::ParamCountGetter(Local<String> property,
                                                       const AccessorInfo &info) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(info.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    return scope.Close(Integer::New(stmt->param_count));
}

/**
 * MysqlStatement#affectedRowsSync() -> Integer
 *
 * Returns the total number of rows changed, deleted, or inserted by the last executed statement
 *
 **/
Handle<Value> MysqlStatement::AffectedRowsSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt->_stmt);

    if (affected_rows == ((my_ulonglong)-1)) {
        return scope.Close(Integer::New(-1));
    }

    return scope.Close(Integer::New(affected_rows));
}

/**
 * MysqlStatement#attrGetSync(attr) -> Boolean|Integer
 * - attr (Integer): Attribute id
 *
 * Used to get the current value of a statement attribute
 **/
Handle<Value> MysqlStatement::AttrGetSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    REQ_INT_ARG(0, attr_integer_key)
    enum_stmt_attr_type attr_key =
                        static_cast<enum_stmt_attr_type>(attr_integer_key);

    // TODO(Sannis): Possible error, see Integer::NewFromUnsigned, 32/64
    unsigned long attr_value; // NOLINT

    if (mysql_stmt_attr_get(stmt->_stmt, attr_key, &attr_value)) {
        return THREXC("This attribute isn't supported by libmysqlclient");
    }

    switch (attr_key) {
        case STMT_ATTR_UPDATE_MAX_LENGTH:
            return scope.Close(Boolean::New(attr_value));
            break;
        case STMT_ATTR_CURSOR_TYPE:
        case STMT_ATTR_PREFETCH_ROWS:
            return scope.Close(Integer::NewFromUnsigned(attr_value));
            break;
        default:
            return THREXC("This attribute isn't supported yet");
    }

    return THREXC("Control reaches end of non-void function :-D");
}

/**
 * MysqlStatement#attrSetSync(attr, value) -> Boolean
 * - attr (Integer): Attribute id
 * - value (Boolean|Integer): Attribute value
 *
 * Set the value of statement attribute.
 * Used to modify the behavior of a prepared statement
 **/
Handle<Value> MysqlStatement::AttrSetSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    REQ_INT_ARG(0, attr_integer_key)
    enum_stmt_attr_type attr_key =
                        static_cast<enum_stmt_attr_type>(attr_integer_key);
    int r = 1;

    switch (attr_key) {
        case STMT_ATTR_UPDATE_MAX_LENGTH:
            {
            REQ_BOOL_ARG(1, attr_bool_value);
            r = mysql_stmt_attr_set(stmt->_stmt, attr_key, &attr_bool_value);
            }
            break;
        case STMT_ATTR_CURSOR_TYPE:
        case STMT_ATTR_PREFETCH_ROWS:
            {
            REQ_UINT_ARG(1, attr_uint_value);
            r = mysql_stmt_attr_set(stmt->_stmt, attr_key, &attr_uint_value);
            }
            break;
        default:
            return THREXC("This attribute isn't supported yet");
    }

    if (r) {
        return THREXC("This attribute isn't supported by libmysqlclient");
    }

    return scope.Close(True());
}

/**
 * MysqlStatement#bindParamsSync(params) -> Boolean
 * - params(Array): Parameters values to bind
 *
 * Binds variables to a prepared statement as parameters
 **/
Handle<Value> MysqlStatement::BindParamsSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    REQ_ARRAY_ARG(0, js_params);

    uint32_t i = 0;
    Local<Value> js_param;

    // For debug
    /*String::Utf8Value *str;
    for (i = 0; i < js_params->Length(); i++) {
        str = new String::Utf8Value(js_params->Get(Integer::New(i))->ToString());
        printf("%d: %s\n", i, **str);
    }*/

    if (js_params->Length() != stmt->param_count) {
        return THREXC("Array length doesn't match number of parameters in prepared statement"); // NOLINT
    }

    int *int_data;
    unsigned int *uint_data;
    double *double_data;
    unsigned long *str_length; // NOLINT
    char *str_data;
    time_t date_timet;
    struct tm date_timeinfo;
    MYSQL_TIME *date_data;

    for (i = 0; i < js_params->Length(); i++) {
        js_param = js_params->Get(i);

        if (js_param->IsUndefined()) {
            return THREXC("All arguments must be defined");
        }

        if (js_param->IsNull()) {
            int_data = new int;
            *int_data = 0;

            stmt->binds[i].buffer_type = MYSQL_TYPE_NULL;
            stmt->binds[i].buffer = int_data;
            // TODO(Sannis): Fix this error
            stmt->binds[i].is_null = 0;
        } else if (js_param->IsInt32()) {
            int_data = new int;
            *int_data = js_param->Int32Value();

            stmt->binds[i].buffer_type = MYSQL_TYPE_LONG;
            stmt->binds[i].buffer = int_data;
            stmt->binds[i].is_null = 0;
            stmt->binds[i].is_unsigned = false;
        } else if (js_param->IsBoolean()) {
            // I assume, booleans are usually stored as TINYINT(1)
            int_data = new int;
            *int_data = js_param->BooleanValue() ? 1 : 0;

            stmt->binds[i].buffer_type = MYSQL_TYPE_TINY;
            stmt->binds[i].buffer = int_data;
            stmt->binds[i].is_null = 0;
            stmt->binds[i].is_unsigned = false;
        } else if (js_param->IsUint32()) {
            uint_data = new unsigned int;
            *uint_data = js_param->Uint32Value();

            stmt->binds[i].buffer_type = MYSQL_TYPE_LONG;
            stmt->binds[i].buffer = uint_data;
            stmt->binds[i].is_null = 0;
            stmt->binds[i].is_unsigned = true;
        } else if (js_param->IsNumber()) {
            double_data = new double;
            *double_data = js_param->NumberValue();

            stmt->binds[i].buffer_type = MYSQL_TYPE_DOUBLE;
            stmt->binds[i].buffer = double_data;
            stmt->binds[i].is_null = 0;
        } else if (js_param->IsDate()) {
            date_data = new MYSQL_TIME;
            date_timet = static_cast<time_t>(js_param->NumberValue()/1000);
            if (!gmtime_r(&date_timet, &date_timeinfo)) {
                return THREXC("Error occured in gmtime_r()");
            }
            date_data->year = date_timeinfo.tm_year + 1900;
            date_data->month = date_timeinfo.tm_mon + 1;
            date_data->day = date_timeinfo.tm_mday;
            date_data->hour = date_timeinfo.tm_hour;
            date_data->minute = date_timeinfo.tm_min;
            date_data->second = date_timeinfo.tm_sec;

            stmt->binds[i].buffer_type = MYSQL_TYPE_DATETIME;
            stmt->binds[i].buffer = date_data;
            stmt->binds[i].is_null = 0;
        } else {  // js_param->IsString() and other
            // TODO(Sannis): Simplify this if possible
            str_data = strdup(**(new String::Utf8Value(js_param->ToString())));
            str_length = new unsigned long; // NOLINT
            *str_length = js_param->ToString()->Length();

            stmt->binds[i].buffer_type = MYSQL_TYPE_STRING;
            stmt->binds[i].buffer =  str_data;
            stmt->binds[i].is_null = 0;
            stmt->binds[i].length = str_length;
        }
    }

    if (mysql_stmt_bind_param(stmt->_stmt, stmt->binds)) {
      return scope.Close(False());
    }

    return scope.Close(True());
}

/**
 * MysqlStatement#closeSync() -> Boolean
 *
 * Closes a prepared statement
 **/
Handle<Value> MysqlStatement::CloseSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    if (mysql_stmt_close(stmt->_stmt)) {
        return scope.Close(False());
    }

    stmt->_stmt = NULL;

    return scope.Close(True());
}

/**
 * MysqlStatement#dataSeekSync(offset)
 * -offset (Integer): Offset value
 *
 * Seeks to an arbitrary row in statement result set
 **/
Handle<Value> MysqlStatement::DataSeekSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;
    MYSQLSTMT_MUSTBE_STORED;

    REQ_NUMBER_ARG(0, offset_double)
    REQ_UINT_ARG(0, offset_uint)

    if (offset_double < 0 || offset_uint >= mysql_stmt_num_rows(stmt->_stmt)) {
        return THREXC("Invalid row offset");
    }

    mysql_stmt_data_seek(stmt->_stmt, offset_uint);

    return Undefined();
}

/**
 * MysqlStatement#errnoSync() -> Integer
 *
 * Returns the error code for the most recent statement call
 **/
Handle<Value> MysqlStatement::ErrnoSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    uint32_t errno = mysql_stmt_errno(stmt->_stmt);

    return scope.Close(Integer::New(errno));
}

/**
 * MysqlStatement#errorSync() -> String
 *
 * Returns a string description for last statement error
 **/
Handle<Value> MysqlStatement::ErrorSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    const char *error = mysql_stmt_error(stmt->_stmt);

    return scope.Close(V8STR(error));
}

/**
 * MysqlStatement#executeSync() -> Boolean
 *
 * Executes a prepared query
 **/
Handle<Value> MysqlStatement::ExecuteSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    if (mysql_stmt_execute(stmt->_stmt)) {
        return scope.Close(False());
    }

    return scope.Close(True());
}

/**
 * MysqlStatement#fetchAllSync() -> Object
 *
 * Returns row data from statement result
 **/
Handle<Value> MysqlStatement::FetchAllSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.This());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    /* Get meta data for binding buffers */

    unsigned int field_count = mysql_stmt_field_count(stmt->_stmt);

    uint32_t i = 0, j = 0;
    unsigned long length[field_count];
    int row_count = 0;
    my_bool is_null[field_count];
    MYSQL_BIND bind[field_count];
    MYSQL_RES *meta;
    MYSQL_FIELD *fields;

    /* Buffers */
    int int_data[field_count];
    signed char tiny_data[field_count];
    double double_data[field_count];
    char str_data[field_count][64];
    MYSQL_TIME date_data[field_count];
    memset(date_data, 0, sizeof(date_data));

    memset(bind, 0, sizeof(bind));

    meta = mysql_stmt_result_metadata(stmt->_stmt);

    fields = meta->fields;
    while (i < field_count) {
        bind[i].buffer_type = fields[i].type;

        switch (fields[i].type) {
            case MYSQL_TYPE_NULL:
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_LONGLONG:
            case MYSQL_TYPE_INT24:
                bind[i].buffer = &int_data[i];
                break;
            case MYSQL_TYPE_TINY:
                bind[i].buffer = &tiny_data[i];
                break;
            case MYSQL_TYPE_FLOAT:
            case MYSQL_TYPE_DOUBLE:
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL:
                bind[i].buffer = &double_data[i];
                break;
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_VARCHAR:
                bind[i].buffer = (char *) str_data[i];
                bind[i].buffer_length = fields[i].length;
                break;
            case MYSQL_TYPE_YEAR:
            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_NEWDATE:
            case MYSQL_TYPE_TIME:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_TIMESTAMP:
                bind[i].buffer = (char *) &date_data[i];
                break;
        }

        bind[i].is_null = &is_null[i];
        bind[i].length = &length[i];
        i++;
    }

    /* If error on binding return null */
    if (mysql_stmt_bind_result(stmt->_stmt, bind)) {
        return scope.Close(Null());
    }

    /* If error on buffering results return null */
    if (mysql_stmt_store_result(stmt->_stmt)) {
        return scope.Close(Null());
    }

    Local<Array> js_result_rows = Array::New();
    Local<Object> js_result_row;
    Handle<Value> js_result;

    row_count = mysql_stmt_num_rows(stmt->_stmt);

    /* If no rows, return empty array */
    if (!row_count) {
        return scope.Close(js_result_rows);
    }

    i = 0;
    while (mysql_stmt_fetch(stmt->_stmt) != MYSQL_NO_DATA) {
        js_result_row = Object::New();

        j = 0;
        while (j < field_count) {
            switch(fields[j].type) {
                case MYSQL_TYPE_NULL:
                case MYSQL_TYPE_SHORT:
                case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_LONGLONG:
                case MYSQL_TYPE_INT24:
                    js_result = Integer::New(int_data[j]);
                    break;
                case MYSQL_TYPE_TINY:
                    if (length[j] == 1) {
                        js_result = Boolean::New(tiny_data[j] != 0);
                    } else {
                        js_result = Integer::NewFromUnsigned(tiny_data[j]);
                    }
                    break;
                case MYSQL_TYPE_FLOAT:
                case MYSQL_TYPE_DOUBLE:
                    js_result = Number::New(double_data[j]);
                    break;
                case MYSQL_TYPE_DECIMAL:
                case MYSQL_TYPE_NEWDECIMAL:
                    js_result = Number::New(double_data[j])->ToString();
                    break;
                case MYSQL_TYPE_STRING:
                case MYSQL_TYPE_VAR_STRING:
                case MYSQL_TYPE_VARCHAR:
                    js_result = V8STR2(str_data[j], length[j]);
                    break;
                case MYSQL_TYPE_YEAR:
                case MYSQL_TYPE_DATE:
                case MYSQL_TYPE_NEWDATE:
                case MYSQL_TYPE_TIME:
                case MYSQL_TYPE_DATETIME:
                case MYSQL_TYPE_TIMESTAMP:
                    MYSQL_TIME ts = date_data[j];
                    time_t rawtime;
                    struct tm * datetime;
                    time(&rawtime);
                    datetime = localtime(&rawtime);
                    datetime->tm_year = ts.year - 1900;
                    datetime->tm_mon = ts.month - 1;
                    datetime->tm_mday = ts.day;
                    datetime->tm_hour = ts.hour;
                    datetime->tm_min = ts.minute;
                    datetime->tm_sec = ts.second;
                    time_t timestamp = mktime(datetime);

                    js_result = Date::New(1000 * (double) timestamp);
                    break;
            }

            js_result_row->Set(V8STR(fields[j].name), js_result);
            j++;
        }

        js_result_rows->Set(Integer::NewFromUnsigned(i), js_result_row);
        i++;
    }

    return scope.Close(js_result_rows);
}

/**
 * MysqlStatement#fieldCountSync() -> Integer
 *
 * Returns the number of field in the given statement
 **/
Handle<Value> MysqlStatement::FieldCountSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    return scope.Close(Integer::New(mysql_stmt_field_count(stmt->_stmt)));
}

/**
 * MysqlStatement#freeResultSync() -> Boolean
 *
 * Frees stored result memory for the given statement handle
 **/
Handle<Value> MysqlStatement::FreeResultSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    return scope.Close(!mysql_stmt_free_result(stmt->_stmt) ? True() : False());
}

/**
 * MysqlStatement#lastInsertIdSync() -> Integer
 *
 * Get the ID generated from the previous INSERT operation
 **/
Handle<Value> MysqlStatement::LastInsertIdSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    return scope.Close(Integer::New(mysql_stmt_insert_id(stmt->_stmt)));
}

/**
 * MysqlStatement#numRowsSync() -> Integer
 *
 * Return the number of rows in statements result set
 **/
Handle<Value> MysqlStatement::NumRowsSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;
    MYSQLSTMT_MUSTBE_STORED;  // TODO(Sannis): Or all result already fetched!

    return scope.Close(Integer::New(mysql_stmt_num_rows(stmt->_stmt)));
}

/**
 * MysqlStatement#prepareSync(query) -> Boolean
 * - query (String): Query for prepare
 *
 * Prepare statement by given query
 **/
Handle<Value> MysqlStatement::PrepareSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    REQ_STR_ARG(0, query)

    // TODO(Sannis): Smth else? close/reset
    stmt->prepared = false;

    unsigned long int query_len = args[0]->ToString()->Utf8Length();

    if (mysql_stmt_prepare(stmt->_stmt, *query, query_len)) {
        return scope.Close(False());
    }

    if (stmt->binds) {
        delete[] stmt->binds;
    }

    stmt->param_count = mysql_stmt_param_count(stmt->_stmt);

    if (stmt->param_count > 0) {
        stmt->binds = new MYSQL_BIND[stmt->param_count];
        memset(stmt->binds, 0, stmt->param_count*sizeof(MYSQL_BIND));

        // TODO(Sannis): Smth else?
    }

    stmt->prepared = true;

    return scope.Close(True());
}

/**
 * MysqlStatement#resetSync() -> Boolean
 *
 * Resets a prepared statement
 **/
Handle<Value> MysqlStatement::ResetSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;


    if (mysql_stmt_reset(stmt->_stmt)) {
        return scope.Close(False());
    }

    return scope.Close(True());
}

/**
 * MysqlStatement#resultMetadataSync() -> MysqlResult
 *
 * Returns result set metadata from a prepared statement
 **/
Handle<Value> MysqlStatement::ResultMetadataSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    MYSQL_RES *my_result = mysql_stmt_result_metadata(stmt->_stmt);

    if (!my_result) {
        return scope.Close(False());
    }

    const int argc = 3;
    Local<Value> argv[argc];
    argv[0] = External::New(stmt->_stmt->mysql); // MySQL connection handle
    argv[1] = External::New(my_result);
    argv[2] = Integer::New(mysql_stmt_field_count(stmt->_stmt));
    Persistent<Object> js_result(MysqlResult::constructor_template->
                             GetFunction()->NewInstance(argc, argv));

    return scope.Close(js_result);
}

/**
 * MysqlStatement#sendLongDataSync(parameterNumber, data) -> Boolean
 * - parameterNumber (Integer): Parameter number, beginning with 0
 * - data (String): Data
 *
 * Send parameter data to the server in blocks (or "chunks")
 **/
Handle<Value> MysqlStatement::SendLongDataSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    REQ_INT_ARG(0, parameter_number);
    REQ_STR_ARG(1, data);

    if (mysql_stmt_send_long_data(stmt->_stmt,
                                  parameter_number, *data, data.length())) {
        return scope.Close(False());
    }

    return scope.Close(True());
}

/**
 * MysqlStatement#sqlStateSync() -> String
 *
 * Returns SQLSTATE error from previous statement operation
 **/
Handle<Value> MysqlStatement::SqlStateSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;

    return scope.Close(V8STR(mysql_stmt_sqlstate(stmt->_stmt)));
}

/**
 * MysqlStatement#storeResultSync() -> Boolean
 *
 * Transfers a result set from a prepared statement
 **/
Handle<Value> MysqlStatement::StoreResultSync(const Arguments& args) {
    HandleScope scope;

    MysqlStatement *stmt = OBJUNWRAP<MysqlStatement>(args.Holder());

    MYSQLSTMT_MUSTBE_INITIALIZED;
    MYSQLSTMT_MUSTBE_PREPARED;

    if (mysql_stmt_store_result(stmt->_stmt) != 0) {
        return scope.Close(False());
    }

    stmt->stored = true;

    return scope.Close(True());
}
