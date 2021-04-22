
#ifndef CC_VMPARSER_H
#define CC_VMPARSER_H

#include <iostream>
#include <string.h>
#include <assert.h>
    
#include "./cparse/shunting-yard.h"
#include "./cparse/shunting-yard-exceptions.h"

typedef packToken returnState;

class Statement
{
protected:
    virtual void _compile(const char *code, const char **rest,
                          TokenMap parent_scope) = 0;
    virtual packToken _exec(TokenMap scope) const = 0;

public:
    virtual ~Statement() {}
    void compile(const char *code, const char **rest = 0,
                 TokenMap parent_scope = &TokenMap::empty)
    {
        return _compile(code, rest, parent_scope);
    }

    packToken exec(TokenMap scope) const { 
        return _exec(scope); 
    }

    // This will allow each statement to be copied:
    virtual Statement *clone() const = 0;
};

/* * * * * ExpStatement Class * * * * */
class ExpStatement : public Statement
{
    calculator expr;

private:
    void _compile(const char *code, const char **rest, TokenMap parent_scope)
    {
        // The string ";}\n" list the delimiters I want for my programming language.
        // Feel free to change it to something more adequate for your own language.
        expr.compile(code, parent_scope, ";}\n", &code);

        // Skip the delimiter character:
        if (*code && *code != '}')
            ++code;

        if (rest)
            *rest = code;
    }
    packToken _exec(TokenMap scope) const
    {
        packToken r =  expr.eval(scope);
        return r;
    }

public:
    ExpStatement() {}
    ExpStatement(const char *code, const char **rest = 0,
                 TokenMap parent_scope = &TokenMap::empty)
    {
        _compile(code, rest, parent_scope);
    }
    virtual Statement *clone() const
    {
        return new ExpStatement(*this);
    }
};


/* * * * * BlockStatement Class * * * * */
class BlockStatement : public Statement
{
public:
    typedef std::map<std::string, Statement *(*)()> statementMap_t;

    // Associate each type of statement with a keyword:
    static statementMap_t &statementMap()
    {
        static statementMap_t map;
        return map;
    }

    // Use this to register new statements on statementsMap.
    template <typename T>
    static Statement *factory() { return new T(); }

private:
    typedef std::vector<Statement *> codeBlock_t;

public:
    codeBlock_t list;

private:
    void cleanList(codeBlock_t *list)
    {
        for (auto stmt : *list)
        {
            delete stmt;
        }

        list->clear();
    }
private:
    void _compile(const char *code, const char **rest, TokenMap parent_scope)
    {
        // Make sure the list is empty:
        cleanList(&list);

        while (isspace(*code))
            ++code;

        if (*code == '{')
        {

            // Find the next non-blank character:
            ++code;
            while (isspace(*code))
                ++code;

            // Parse each statement of the block:
            while (*code && *code != '}')
            {
                // Ignore empty statements:
                if (strchr(";\n", *code))
                {
                    ++code;
                }
                else
                {
                    list.push_back(buildStatement(&code, parent_scope));
                }

                // Discard blank spaces:
                while (isspace(*code))
                    ++code;
            }

            if (*code == '}')
            {
                ++code;
            }
            else
            {
                throw syntax_error("Missing a '}' somewhere on the code!");
            }
        }
        else
        {
            list.push_back(buildStatement(&code, parent_scope));
        }

        if (rest)
            *rest = code;
    }
protected:
    packToken _exec(TokenMap scope) const { assert(false); }  // should not be here

    // Decide what type of statement to build:
    Statement *buildStatement(const char **source, TokenMap scope)
    {
        const char *code = *source;

        // could not be a simple block statement {...}, it is always AND {...}, OR {...} or FOR(x : y) {...} 
        // If it is a block statement:
        /* if (*code == '{')
        {
            Statement *stmt = new BlockStatement();
            stmt->compile(code, source, scope);
            return stmt;
        } */

        // Parse the first word of the text:
        std::string name = rpnBuilder::parseVar(code);
        // Check if it is a reserved word:
        statementMap_t &stmt_map = statementMap();
        auto it = stmt_map.find(name);
        if (it != stmt_map.end())
        {
            // If it is parse it and return:
            Statement *stmt = it->second();
            stmt->compile(code + name.size(), source, scope);
            return stmt;
        }

        // Return a normal statement:
        return new ExpStatement(code, source, scope);
    }
public:
    BlockStatement() {}

    // Implement The Big 3, for safely copying:
    BlockStatement(const BlockStatement &other)
    {
        for (const Statement *stmt : other.list)
        {
            list.push_back(stmt->clone());
        }
    }
    ~BlockStatement()
    {
        cleanList(&list);
    }
    BlockStatement &operator=(const BlockStatement &other)
    {
        cleanList(&list);
        for (const Statement *stmt : other.list)
        {
            list.push_back(stmt->clone());
        }
        return *this;
    }

    virtual Statement *clone() const
    {
        return new BlockStatement(*this);
    }
};


// base class for rule statement
class RuleStatementBase : public Statement
{
protected:
    BlockStatement _blockstmt;

protected:
    void _compile(const char *code, const char **rest, TokenMap parent_scope)
    {
        while (isspace(*code))
            ++code;

        _blockstmt.compile(code, &code, parent_scope);

        while (isspace(*code))
            ++code;

        if (rest)
            *rest = code;
    }

public:
    //RuleStatementBase() {}
};

/* * * * * RuleStatementAnd Or Classes * * * * */
class RuleStatementAnd : public RuleStatementBase
{
    returnState _exec(TokenMap scope) const
    {
        // Returned value:
        bool result = true;
        for (const auto stmt : _blockstmt.list)
        {
            // In a more complete implementation, `rv` should
            // be checked for "return" or "throw" behaviors.
            packToken rstmt = stmt->exec(scope);

            result = result && rstmt.asBool();
        }
        return result;
    }


public:
    /*bool trycompile(const char *code)
    {
        while (isspace(*code))
            ++code;
        std::string name = rpnBuilder::parseVar(code);
        if (name == "AND")
        {
            _compile(code + name.size(), NULL, TokenMap::empty);
            return true;
        }
        else
        {
            return false;
        }
    }*/
    RuleStatementAnd() {}
    RuleStatementAnd(const RuleStatementAnd &other)
    {
        _blockstmt = *static_cast<BlockStatement*>( other._blockstmt.clone() );
    }
    virtual Statement *clone() const
    {
        return new RuleStatementAnd(*this);
    }
};

class RuleStatementOr : public RuleStatementBase
{
    returnState _exec(TokenMap scope) const
    {
        // Returned value:
        packToken rv = false;
        for (const auto stmt : _blockstmt.list)
        {
            // In a more complete implementation, `rv` should
            // be checked for "return" or "throw" behaviors.
            rv = stmt->exec(scope);
            if (rv.asBool())
                break;
        }
        return rv;
    }

public:
    /*bool trycompile(const char *code)
    {
        while (isspace(*code))
            ++code;
        std::string name = rpnBuilder::parseVar(code);
        if (name == "OR")
        {
            _compile(code + name.size(), NULL, TokenMap::empty);
            return true;
        }
        else
        {
            return false;
        }
    }*/
    RuleStatementOr() {}
    RuleStatementOr(const RuleStatementOr &other)
    {
        _blockstmt = *static_cast<BlockStatement*>( other._blockstmt.clone() );
    }
    virtual Statement *clone() const
    {
        return new RuleStatementOr(*this);
    }
};

// toplevel rule statement
class RuleStatement : public Statement
{
public:
    typedef std::map<std::string, Statement *(*)()> statementMap_t;

    // Associate each type of statement with a keyword:
    static statementMap_t &statementMap()
    {
        static statementMap_t map;
        return map;
    }

    // Use this to register new statements on statementsMap.
    template <typename T>
    static Statement *factory() { return new T(); }

protected:
    Statement *_rulestmt;

protected:
    void _compile(const char *code, const char **rest, TokenMap parent_scope)
    {
        while (isspace(*code))
            ++code;

        std::string name = rpnBuilder::parseVar(code);
        // Check if it is a reserved word:
        statementMap_t &stmt_map = statementMap();
        auto it = stmt_map.find(name);
        if (it != stmt_map.end())
        {
            // If it is create stmt and parse it:
            _rulestmt = it->second();
            _rulestmt->compile(code + name.size(), 0, parent_scope);
            return;
        }
        throw syntax_error("No valid rule statement found!");
    }

    returnState _exec(TokenMap scope) const
    {
        if (_rulestmt)
            return _rulestmt->exec(scope);
        else
            return packToken();
    }

public:
    RuleStatement() { _rulestmt = NULL; }
    ~RuleStatement() {
        if (_rulestmt)
            delete _rulestmt;
    }
    RuleStatement(const RuleStatement &other)
    {
        _rulestmt = other._rulestmt->clone();
    }
    virtual Statement *clone() const
    {
        return new RuleStatement(*this);
    }
};

// for(it : array) {...}  statement
class ForStatement : public Statement
{
    std::string iteratorname;
    //std::string arrayname;
    calculator arraycond;
    BlockStatement _blockstmt;

private:
    void _compile(const char *code, const char **rest, TokenMap parent_scope)
    {

        while (isspace(*code))
            ++code;

        if (*code != '(')
        {
            throw syntax_error("Expected '(' after `FOR` statement!");
        }
        ++ code;

        while (isspace(*code)) ++code;
        iteratorname = rpnBuilder::parseVar(code);
        code += iteratorname.length();
        while (isspace(*code))  ++code;

        if (*code != ':')
        {
            throw syntax_error("Expected ':' after iterator name!");
        }
        ++ code;

        while (isspace(*code))  ++code;
        //arrayname = rpnBuilder::parseVar(code);
        //code += arrayname.length();
        arraycond.compile(code, parent_scope, ")", &code);

        while (isspace(*code))  ++code;

        if (*code != ')')
        {
            throw syntax_error("Missing ')' after array name!");
        }

        _blockstmt.compile(code + 1, &code, parent_scope);

        while (isspace(*code))
            ++code;

        if (rest)
            *rest = code;
    }

    returnState _exec(TokenMap scope) const
    {
        TokenMap localscope = scope.getChild();
        packToken array = arraycond.eval(scope);
        Iterator* it;
        
        /*if (scope[arrayname]->type == MAP)
            it = scope[arrayname].asMap().getIterator();
        else if (scope[arrayname]->type == LIST)
            it = scope[arrayname].asList().getIterator();
        else if (scope[arrayname]->type == TUPLE)
            it = scope[arrayname].asTuple().getIterator();
        else if (scope[arrayname]->type == STUPLE)
            it = scope[arrayname].asSTuple().getIterator();
        else 
            throw syntax_error("not an iterator object!");*/

        if (array->type == MAP)
            it = array.asMap().getIterator();
        else if (array->type == LIST)
            it = array.asList().getIterator();
        else if (array->type == TUPLE)
            it = array.asTuple().getIterator();
        else if (array->type == STUPLE)
            it = array.asSTuple().getIterator();
        else 
            throw syntax_error("not an iterator object!");

        std::cerr << __func__ << " array->type=" << (int)array->type << std::endl;
        packToken *next = it->next();
        while(next) 
        {
            std::cerr << __func__ << " next=" << *next << std::endl;
            localscope[iteratorname] = *next;
            for (const auto stmt : _blockstmt.list)
            {
                // In a more complete implementation, result should
                // be checked for "return" or "throw" behaviors.
                stmt->exec(localscope);
            }
            next = it->next();  //move iterator to the next element
        }
        return true; // FOR always evaluates to true (other options: the value of the last statement)
    }
public:
    ForStatement() {}
    virtual Statement *clone() const
    {
        return new ForStatement(*this);
    }
};

struct MyStartup
{
    MyStartup()
    {
        // init block statement inside parser 
        auto &statementMap = BlockStatement::statementMap();

        statementMap["AND"] = BlockStatement::factory<RuleStatementAnd>;
        statementMap["OR"] = BlockStatement::factory<RuleStatementOr>;
        statementMap["FOR"] = BlockStatement::factory<ForStatement>;

        // init top level parser:
        auto &statementMapRule = RuleStatement::statementMap();

        statementMapRule["AND"] = RuleStatement::factory<RuleStatementAnd>;
        statementMapRule["OR"] = RuleStatement::factory<RuleStatementOr>;

    }
};


/*
int main()
{
    TokenMap global;
    RuleStatementAnd parseAnd;
    RuleStatementOr parseOr;
    struct MyStartup startup; // init

    global["getActiveChain"] = CppFunction(&get_active_chain, {}, "");
    BASE_chain["height"] = CppFunction(&chain_height);

    global["getEval"] = CppFunction(&get_eval, {}, "");
    global["getEvalTx"] = CppFunction(&get_eval_tx, {}, "");
    //BASE_tx["vin"] = CppFunction(&get_tx_vin, {"index"}, "");
    //BASE_vin["hash"] = CppFunction(&get_tx_vin_hash, {}, "");
    BASE_tx["vout"] = CppFunction(&get_tx_vout, {"index"}, "");
    BASE_vout["amount"] = CppFunction(&get_tx_vout_amount, {}, "");

    const char *code = "AND {\n"
        " chain = getActiveChain();\n"
        " h = chain.height();\n"
        " chain2 = getActiveChain();\n"
        " h = chain2.height();\n"
        " h = chain.height();\n"
        " c1 = getEvalTx().vin[1].hash; "
        " vlen=getEvalTx().vin.len(); "
        " c2 = getEvalTx().vout(0).amount(); "
        " n = 0;"
        " vins = getEvalTx().vin;"
        " vinh = \"00\"; "
        " FOR(v : getEvalTx().vin) { n = n+1; vinh = v.hash }\n "
        "  a = 2;\n"
        " s = 20;"
        "  OR {\n"
        "    b = True;\n"
        " s = 21\n"
        "  } \n"
        "}";

    bool r;
    if (parseAnd.trycompile(code))
        r = parseAnd.exec(global).asBool();
    else if (parseOr.trycompile(code))
        r = parseOr.exec(global).asBool();
    else
    {
        std::cerr << "could not parse" << std::endl;
        return 1;
    }
    std::cout << "result=" << r << std::endl;
    std::cout << "h=" << global["h"] << std::endl; // h
    std::cout << "a=" << global["a"] << std::endl; // 2
    std::cout << "b=" << global["b"] << std::endl; // true
    std::cout << "s=" << global["s"] << std::endl; // 20 because of OR
    std::cout << "c1=" << global["c1"] << std::endl;
    std::cout << "c2=" << global["c2"] << std::endl;
    std::cout << "vlen=" << global["vlen"] << std::endl;
    std::cout << "n=" << global["n"] << std::endl;
    std::cout << "vinh=" << global["vinh"] << std::endl;

}
*/

#endif // #ifndef CC_VMPARSER_H
