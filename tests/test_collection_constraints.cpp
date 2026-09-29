#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/interpreter/runtime/collection_constraints.hpp"
#include "lumiere/interpreter/runtime/type_aliases.hpp"
#include "lumiere/interpreter/vm/vm.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"
#include <gtest/gtest.h>

namespace lumiere
{

TEST(CollectionConstraints, AliasExpansionPreservesStructureAndRejectsCycles)
{
    const auto named = [](const std::string &name) {
        return TypeExpr::named(Token(TokenType::IDENT, name, 0, 0));
    };
    TypeAliasTable aliases{{"Nombre", named("Entier")}};
    const auto dictionary = TypeExpr::generic(Token(TokenType::IDENT, "Dictionnaire", 0, 0),
                                             "Dictionnaire", {named("Nombre"), named("Nombre")});
    const auto resolved = resolve_type_aliases(dictionary, aliases);
    EXPECT_EQ(resolved.kind, TypeExprKind::GENERIC);
    EXPECT_EQ(resolved.to_string(), "Dictionnaire[Entier,Entier]");
    EXPECT_EQ(dictionary.to_string(), "Dictionnaire[Nombre,Nombre]");
    aliases.emplace("A", named("B"));
    aliases.emplace("B", named("A"));
    EXPECT_THROW(resolve_type_aliases(named("A"), aliases), std::invalid_argument);
}

TEST(CollectionConstraints, EnvironmentAliasesResolveInDefinitionScope)
{
    const auto named = [](const std::string &name) {
        return TypeExpr::named(Token(TokenType::IDENT, name, 0, 0));
    };
    auto outer = make_ref<Environment>();
    outer->define_type_alias("Base", named("Entier"));
    outer->define_type_alias("Nombre", named("Base"));
    Environment inner(outer);
    inner.define_type_alias("Base", named("Texte"));
    EXPECT_EQ(inner.resolve_type_aliases(named("Nombre")).to_string(), "Entier");
    EXPECT_EQ(inner.resolve_type_aliases(named("Base")).to_string(), "Texte");
    inner.define_type_alias("A", named("B"));
    inner.define_type_alias("B", named("A"));
    EXPECT_THROW(inner.resolve_type_aliases(named("A")), std::invalid_argument);
}

// Exercise the runtime boundary independently of the CLI's static analysis.
// Embedders and native modules can attach annotations directly.
static void execute_collection_program(const std::string &body, bool vm)
{
    const std::string source = "fonction principal() {\n" + body + "\n}";
    Lexer lexer(source);
    Parser parser(lexer.tokenise());
    Program program;
    program.statements = parser.parse();
    program.source_text = source;
    ASSERT_FALSE(parser.had_error());
    if (vm)
        VM().execute(program);
    else
        TreeWalker().execute(program);
}

TEST(CollectionConstraints, BothEnginesRestoreBlockLocalAliasesAndCaptureThemInFunctions)
{
    const std::string declarations =
        "type Element = Entier\n"
        "{\n type Element = Texte\n"
        "soit identite = fonction(valeur: Element) -> Element { retourne valeur }\n"
        "soit texte: Element = identite(\"correct\")\n}\n";
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        EXPECT_NO_THROW(execute_collection_program(declarations + "soit nombre: Element = 1", vm));
        EXPECT_THROW(execute_collection_program(declarations + "soit nombre: Element = \"incorrect\"", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, BothEnginesResolveChainedAliasesInTheirDefinitionScope)
{
    const std::string declarations =
        "type Base = Entier\n"
        "type Intermédiaire = Base\n"
        "type Nombre = Intermédiaire\n"
        "{\n type Base = Texte\n type Intermédiaire = Base\n";
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        EXPECT_NO_THROW(execute_collection_program(
            declarations + "soit valeur: Nombre = 7\n}", vm));
        EXPECT_THROW(execute_collection_program(
            declarations + "soit valeur: Nombre = \"incorrect\"\n}", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, BothEnginesKeepReturnedFunctionsAliasScope)
{
    const std::string declarations =
        "fonction fabriquer() {\n type Element = Entier\n"
        "retourne fonction(valeur: Element) -> Element { retourne valeur }\n}\n"
        "soit identite = fabriquer()\ntype Element = Texte\n";
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        EXPECT_NO_THROW(execute_collection_program(declarations + "identite(7)", vm));
        EXPECT_THROW(execute_collection_program(declarations + "identite(\"incorrect\")", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, BothEnginesKeepMethodsFromReturnedLocalClassesAlive)
{
    const std::string source =
        "fonction fabriquer() {\nsoit valeur = 7\n"
        "classe Locale { fonction lire() -> Entier { retourne valeur } }\n"
        "retourne Locale()\n}\n"
        "soit instance = fabriquer()\nsoit methode = instance.lire\nmethode()\n";
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        EXPECT_NO_THROW(execute_collection_program(source, vm));
    }
}

TEST(CollectionConstraints, IncrementalAnonymousUnitsKeepNominalDeclarationsDistinct)
{
    TreeWalker interpreter;
    std::vector<std::unique_ptr<Program>> programs;
    const auto submit = [&](const std::string &source) -> std::optional<Value> {
        Lexer lexer(source);
        Parser parser(lexer.tokenise());
        auto program = std::make_unique<Program>();
        program->statements = parser.parse();
        program->source_text = source;
        EXPECT_FALSE(parser.had_error());
        const auto result = interpreter.execute_incremental(*program);
        programs.push_back(std::move(program));
        return result;
    };
    const auto factory = [](const char suffix) {
        return "fonction creer" + std::string(1, suffix) + "() {\n"
               "classe Objet { fonction meme(autre: Universel) -> Logique { retourne autre est Objet } }\n"
               "retourne Objet()\n}\n";
    };

    EXPECT_FALSE(submit(factory('A')).has_value());
    EXPECT_FALSE(submit(factory('B')).has_value());
    EXPECT_FALSE(submit("soit a = creerA()\n").has_value());
    EXPECT_FALSE(submit("soit b = creerB()\n").has_value());
    const auto result = submit("a.meme(b)\n");
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_logique());
    EXPECT_FALSE(result->as_logique());
}

TEST(CollectionConstraints, BothEnginesResolveFieldAliasesInTheirDeclaringScope)
{
    const std::string declarations =
        "fonction fabriquer() {\ntype Nombre = Entier\n"
        "classe Locale { base: Nombre }\nretourne Locale\n}\n"
        "soit ClasseLocale = fabriquer()\ntype Nombre = Texte\n"
        "soit objet = ClasseLocale(base: 4)\n";
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        EXPECT_NO_THROW(execute_collection_program(declarations + "objet.base = 6", vm));
        EXPECT_THROW(execute_collection_program(declarations + "objet.base = \"incorrect\"", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, BothEnginesPreserveConcreteContractsThroughUniversalAliases)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        const std::string declarations =
            "soit valeurs: Liste[Entier] = [1]\n"
            "soit alias: Liste[Universel] = valeurs\n";
        EXPECT_NO_THROW(execute_collection_program(declarations + "alias.ajouter(2)", vm));
        EXPECT_THROW(execute_collection_program(declarations + "alias.ajouter(\"incorrect\")", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, BothEnginesProtectExistingAndInsertedNestedLists)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        for (const std::string declarations : {
                 "soit enfant = [1]\nsoit conteneur: Liste[Liste[Entier]] = [enfant]\n",
                 "soit enfant = [1]\nsoit conteneur: Liste[Liste[Entier]] = []\nconteneur.ajouter(enfant)\n",
                 "soit enfant = [1]\nsoit conteneur: Dictionnaire[Texte, Liste[Entier]] = {\"a\": enfant}\n",
                 "soit enfant = [1]\nsoit conteneur: Dictionnaire[Texte, Liste[Entier]] = {}\nconteneur[\"a\"] = enfant\n"})
        {
            SCOPED_TRACE(declarations);
            EXPECT_NO_THROW(execute_collection_program(declarations + "enfant.ajouter(2)", vm));
            EXPECT_THROW(execute_collection_program(declarations + "enfant.ajouter(\"incorrect\")", vm), RuntimeError);
        }
    }
}

TEST(CollectionConstraints, BothEnginesResolveAliasesBeforeStoringContracts)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        // Alias declarations belong at module scope.
        for (const auto *mutation : {"valeurs.ajouter(2)", "valeurs.ajouter(\"incorrect\")",
                                     "valeurs = [2]", "valeurs = [\"incorrect\"]"})
        {
            const std::string source =
                "type Nombre = Entier\ntype Nombres = Liste[Nombre]\n"
                "fonction principal() {\nsoit valeurs: Nombres = [1]\n" +
                std::string(mutation) + "\n}";
            Lexer lexer(source);
            Parser parser(lexer.tokenise());
            Program program;
            program.statements = parser.parse();
            program.source_text = source;
            ASSERT_FALSE(parser.had_error());
            auto execute = [&] {
                if (vm) VM().execute(program);
                else TreeWalker().execute(program);
            };
            if (std::string(mutation).find("incorrect") != std::string::npos)
                EXPECT_THROW(execute(), RuntimeError);
            else
                EXPECT_NO_THROW(execute());
        }
    }
}

TEST(CollectionConstraints, BothEnginesProtectCollectionAlternativesInUnions)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        for (const auto *type : {"Liste[Entier] | Rien", "Rien | Liste[Entier]"})
        {
            const std::string declarations = "soit valeurs: " + std::string(type) + " = [1]\n";
            EXPECT_NO_THROW(execute_collection_program(declarations + "valeurs.ajouter(2)", vm));
            EXPECT_THROW(execute_collection_program(declarations + "valeurs.ajouter(\"incorrect\")", vm), RuntimeError);
        }
    }
}

TEST(CollectionConstraints, BothEnginesProtectCollectionsInsideSuccessPayloads)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        for (const std::string declarations : {
                 "soit valeurs = [1]\nsoit sortie: Résultat[Liste[Entier], Erreur] = Succès(valeurs)\n",
                 "soit valeurs = [1]\nsoit sorties: Liste[Résultat[Liste[Entier], Erreur]] = [Succès(valeurs)]\n",
                 "soit valeurs = [1]\nsoit sorties: Liste[Résultat[Liste[Entier], Erreur]] = []\nsorties.ajouter(Succès(valeurs))\n"})
        {
            SCOPED_TRACE(declarations);
            EXPECT_NO_THROW(execute_collection_program(declarations + "valeurs.ajouter(2)", vm));
            EXPECT_THROW(execute_collection_program(declarations + "valeurs.ajouter(\"incorrect\")", vm), RuntimeError);
        }
    }
}

TEST(CollectionConstraints, BothEnginesRejectConflictingEmptyCollectionAliases)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        EXPECT_THROW(execute_collection_program(
            "soit valeurs: Liste[Entier] = []\n"
            "soit alias: Liste[Texte] = valeurs", vm), RuntimeError);
        EXPECT_THROW(execute_collection_program(
            "soit valeurs: Dictionnaire[Texte, Entier] = {}\n"
            "soit alias: Dictionnaire[Texte, Texte] = valeurs", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, BothEnginesAllowUniversalContractsToBeRefined)
{
    for (bool vm : {false, true})
    {
        SCOPED_TRACE(vm ? "VM" : "TreeWalker");
        const std::string declarations =
            "soit valeurs: Liste[Universel] = []\n"
            "soit alias: Liste[Entier] = valeurs\n";
        EXPECT_NO_THROW(execute_collection_program(declarations + "valeurs.ajouter(2)", vm));
        EXPECT_THROW(execute_collection_program(declarations + "valeurs.ajouter(\"incorrect\")", vm), RuntimeError);
    }
}

TEST(CollectionConstraints, FailedDictionaryMergeDoesNotPartiallyChangeTheContract)
{
    std::optional<DictConstraint> constraint = DictConstraint{"Universel", "Entier"};
    EXPECT_FALSE(merge_collection_constraint(constraint, DictConstraint{"Texte", "Logique"}));
    EXPECT_EQ(constraint->key_type, "Universel");
    EXPECT_EQ(constraint->value_type, "Entier");
    EXPECT_TRUE(merge_collection_constraint(constraint, DictConstraint{"Texte", "Universel"}));
    EXPECT_EQ(constraint->key_type, "Texte");
    EXPECT_EQ(constraint->value_type, "Entier");
}

TEST(CollectionConstraints, ReannotationPreservesFixedLengthAndSetElementContracts)
{
    std::optional<FixedListConstraint> fixed = FixedListConstraint{"Entier", 2};
    EXPECT_TRUE(merge_collection_constraint(fixed, FixedListConstraint{"Universel", 2}));
    EXPECT_EQ(fixed->element_type, "Entier");
    EXPECT_FALSE(merge_collection_constraint(fixed, FixedListConstraint{"Entier", 3}));
    EXPECT_EQ(fixed->length, 2);
    std::optional<SetConstraint> set = SetConstraint{"Entier"};
    EXPECT_TRUE(merge_collection_constraint(set, SetConstraint{"Universel"}));
    EXPECT_EQ(set->element_type, "Entier");
    EXPECT_FALSE(merge_collection_constraint(set, SetConstraint{"Texte"}));
}

TEST(CollectionConstraints, BelongToValuesRatherThanTheAnnotatingRuntime)
{
    auto list = make_ref<ListeData>();
    auto fixed = make_ref<ListeFixeData>();
    auto dictionary = make_ref<DictData>();
    auto set = make_ref<EnsembleData>();
    {
        TreeWalker runtime;
        runtime.annotate_value(Value::liste(list), "Liste[Entier]", {});
        runtime.annotate_value(Value::liste_fixe(fixed), "ListeFixe[Texte, 0]", {});
        runtime.annotate_value(Value::dictionnaire(dictionary), "Dictionnaire[Texte, Entier]", {});
        runtime.annotate_value(Value::ensemble(set), "Ensemble[Texte]", {});
    }
    ASSERT_TRUE(list->constraint);
    EXPECT_EQ(list->constraint->element_type, "Entier");
    ASSERT_TRUE(fixed->constraint);
    EXPECT_EQ(fixed->constraint->element_type, "Texte");
    EXPECT_EQ(fixed->constraint->length, 0);
    ASSERT_TRUE(dictionary->constraint);
    EXPECT_EQ(dictionary->constraint->key_type, "Texte");
    EXPECT_EQ(dictionary->constraint->value_type, "Entier");
    ASSERT_TRUE(set->constraint);
    EXPECT_EQ(set->constraint->element_type, "Texte");
}

TEST(CollectionConstraints, ReusingAnAddressStartsWithoutAConstraint)
{
    TreeWalker runtime;
    const ListeData *address = nullptr;
    {
        auto list = make_ref<ListeData>();
        address = list.get();
        runtime.annotate_value(Value::liste(list), "Liste[Entier]", {});
        ASSERT_TRUE(list->constraint);
    }

    // The annotated list is gone. A fresh one commonly lands on the address it
    // freed, and must carry nothing over from it. The contract lives on the
    // allocation, so it dies with it.
    const auto replacement = make_ref<ListeData>();
    EXPECT_FALSE(replacement->constraint)
        << "a new collection inherited a dead one's contract"
        << (replacement.get() == address ? ", at the same address" : "");
}

} // namespace lumiere
