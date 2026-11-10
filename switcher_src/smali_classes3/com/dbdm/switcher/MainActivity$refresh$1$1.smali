.class final Lcom/dbdm/switcher/MainActivity$refresh$1$1;
.super Lkotlin/coroutines/jvm/internal/SuspendLambda;
.source "MainActivity.kt"

# interfaces
.implements Lkotlin/jvm/functions/Function2;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/dbdm/switcher/MainActivity$refresh$1;->invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x18
    name = null
.end annotation

.annotation system Ldalvik/annotation/Signature;
    value = {
        "Lkotlin/coroutines/jvm/internal/SuspendLambda;",
        "Lkotlin/jvm/functions/Function2<",
        "Lkotlinx/coroutines/CoroutineScope;",
        "Lkotlin/coroutines/Continuation<",
        "-",
        "Lkotlin/Unit;",
        ">;",
        "Ljava/lang/Object;",
        ">;"
    }
.end annotation

.annotation system Ldalvik/annotation/SourceDebugExtension;
    value = "SMAP\nMainActivity.kt\nKotlin\n*S Kotlin\n*F\n+ 1 MainActivity.kt\ncom/dbdm/switcher/MainActivity$refresh$1$1\n+ 2 fake.kt\nkotlin/jvm/internal/FakeKt\n*L\n1#1,368:1\n1#2:369\n*E\n"
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000\n\n\u0000\n\u0002\u0010\u0002\n\u0002\u0018\u0002\u0010\u0000\u001a\u00020\u0001*\u00020\u0002H\u008a@"
    }
    d2 = {
        "<anonymous>",
        "",
        "Lkotlinx/coroutines/CoroutineScope;"
    }
    k = 0x3
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation

.annotation runtime Lkotlin/coroutines/jvm/internal/DebugMetadata;
    c = "com.dbdm.switcher.MainActivity$refresh$1$1"
    f = "MainActivity.kt"
    i = {}
    l = {}
    m = "invokeSuspend"
    n = {}
    s = {}
.end annotation


# instance fields
.field final synthetic $accJson:Ljava/lang/String;

.field final synthetic $ssaid:Ljava/lang/String;

.field final synthetic $userId:Ljava/lang/String;

.field label:I

.field final synthetic this$0:Lcom/dbdm/switcher/MainActivity;


# direct methods
.method constructor <init>(Lcom/dbdm/switcher/MainActivity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lkotlin/coroutines/Continuation;)V
    .locals 1
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Lcom/dbdm/switcher/MainActivity;",
            "Ljava/lang/String;",
            "Ljava/lang/String;",
            "Ljava/lang/String;",
            "Lkotlin/coroutines/Continuation<",
            "-",
            "Lcom/dbdm/switcher/MainActivity$refresh$1$1;",
            ">;)V"
        }
    .end annotation

    iput-object p1, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iput-object p2, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$ssaid:Ljava/lang/String;

    iput-object p3, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$accJson:Ljava/lang/String;

    iput-object p4, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$userId:Ljava/lang/String;

    const/4 v0, 0x2

    invoke-direct {p0, v0, p5}, Lkotlin/coroutines/jvm/internal/SuspendLambda;-><init>(ILkotlin/coroutines/Continuation;)V

    return-void
.end method


# virtual methods
.method public final create(Ljava/lang/Object;Lkotlin/coroutines/Continuation;)Lkotlin/coroutines/Continuation;
    .locals 7
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Ljava/lang/Object;",
            "Lkotlin/coroutines/Continuation<",
            "*>;)",
            "Lkotlin/coroutines/Continuation<",
            "Lkotlin/Unit;",
            ">;"
        }
    .end annotation

    new-instance v6, Lcom/dbdm/switcher/MainActivity$refresh$1$1;

    iget-object v1, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iget-object v2, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$ssaid:Ljava/lang/String;

    iget-object v3, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$accJson:Ljava/lang/String;

    iget-object v4, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$userId:Ljava/lang/String;

    move-object v0, v6

    move-object v5, p2

    invoke-direct/range {v0 .. v5}, Lcom/dbdm/switcher/MainActivity$refresh$1$1;-><init>(Lcom/dbdm/switcher/MainActivity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lkotlin/coroutines/Continuation;)V

    check-cast v6, Lkotlin/coroutines/Continuation;

    return-object v6
.end method

.method public bridge synthetic invoke(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;
    .locals 1

    check-cast p1, Lkotlinx/coroutines/CoroutineScope;

    check-cast p2, Lkotlin/coroutines/Continuation;

    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->invoke(Lkotlinx/coroutines/CoroutineScope;Lkotlin/coroutines/Continuation;)Ljava/lang/Object;

    move-result-object v0

    return-object v0
.end method

.method public final invoke(Lkotlinx/coroutines/CoroutineScope;Lkotlin/coroutines/Continuation;)Ljava/lang/Object;
    .locals 2
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Lkotlinx/coroutines/CoroutineScope;",
            "Lkotlin/coroutines/Continuation<",
            "-",
            "Lkotlin/Unit;",
            ">;)",
            "Ljava/lang/Object;"
        }
    .end annotation

    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->create(Ljava/lang/Object;Lkotlin/coroutines/Continuation;)Lkotlin/coroutines/Continuation;

    move-result-object v0

    check-cast v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;

    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    invoke-virtual {v0, v1}, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    return-object v0
.end method

.method public final invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;
    .locals 7

    invoke-static {}, Lkotlin/coroutines/intrinsics/IntrinsicsKt;->getCOROUTINE_SUSPENDED()Ljava/lang/Object;

    .line 104
    iget v0, p0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->label:I

    packed-switch v0, :pswitch_data_0

    new-instance p1, Ljava/lang/IllegalStateException;

    const-string v0, "call to \'resume\' before \'invoke\' with coroutine"

    invoke-direct {p1, v0}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw p1

    :pswitch_0
    invoke-static {p1}, Lkotlin/ResultKt;->throwOnFailure(Ljava/lang/Object;)V

    move-object v0, p0

    .line 105
    .local v0, "this":Lcom/dbdm/switcher/MainActivity$refresh$1$1;
    .local p1, "$result":Ljava/lang/Object;
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iget-object v2, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$ssaid:Ljava/lang/String;

    invoke-static {v1, v2}, Lcom/dbdm/switcher/MainActivity;->access$setCurrentSsaid$p(Lcom/dbdm/switcher/MainActivity;Ljava/lang/String;)V

    .line 106
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iget-object v2, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$accJson:Ljava/lang/String;

    invoke-static {v1, v2}, Lcom/dbdm/switcher/MainActivity;->access$setCurrentAccountsJson$p(Lcom/dbdm/switcher/MainActivity;Ljava/lang/String;)V

    .line 107
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v1}, Lcom/dbdm/switcher/MainActivity;->access$getGameUserId$p(Lcom/dbdm/switcher/MainActivity;)I

    move-result v1

    const/16 v2, 0x96

    if-ne v1, v2, :cond_0

    const-string v1, " [VM user 150]"

    goto :goto_0

    :cond_0
    const-string v1, ""

    .line 108
    .local v1, "userLabel":Ljava/lang/String;
    :goto_0
    iget-object v2, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v2}, Lcom/dbdm/switcher/MainActivity;->access$getTvSsaid$p(Lcom/dbdm/switcher/MainActivity;)Landroid/widget/TextView;

    move-result-object v2

    const/4 v3, 0x0

    if-nez v2, :cond_1

    const-string v2, "tvSsaid"

    invoke-static {v2}, Lkotlin/jvm/internal/Intrinsics;->throwUninitializedPropertyAccessException(Ljava/lang/String;)V

    move-object v2, v3

    :cond_1
    iget-object v4, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$ssaid:Ljava/lang/String;

    .end local v1    # "userLabel":Ljava/lang/String;
    if-nez v4, :cond_2

    const-string v4, "n\u00e3o encontrado"

    :cond_2
    new-instance v5, Ljava/lang/StringBuilder;

    invoke-direct {v5}, Ljava/lang/StringBuilder;-><init>()V

    const-string v6, "SSAID"

    invoke-virtual {v5, v6}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v5

    invoke-virtual {v5, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    const-string v5, ": "

    invoke-virtual {v1, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    check-cast v1, Ljava/lang/CharSequence;

    invoke-virtual {v2, v1}, Landroid/widget/TextView;->setText(Ljava/lang/CharSequence;)V

    .line 109
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v1}, Lcom/dbdm/switcher/MainActivity;->access$getTvUserId$p(Lcom/dbdm/switcher/MainActivity;)Landroid/widget/TextView;

    move-result-object v1

    if-nez v1, :cond_3

    const-string v1, "tvUserId"

    invoke-static {v1}, Lkotlin/jvm/internal/Intrinsics;->throwUninitializedPropertyAccessException(Ljava/lang/String;)V

    move-object v1, v3

    :cond_3
    iget-object v2, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->$userId:Ljava/lang/String;

    if-nez v2, :cond_4

    const-string v2, "(sem conta / carregando)"

    :cond_4
    new-instance v4, Ljava/lang/StringBuilder;

    invoke-direct {v4}, Ljava/lang/StringBuilder;-><init>()V

    const-string v5, "user_id: "

    invoke-virtual {v4, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    check-cast v2, Ljava/lang/CharSequence;

    invoke-virtual {v1, v2}, Landroid/widget/TextView;->setText(Ljava/lang/CharSequence;)V

    .line 110
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v1}, Lcom/dbdm/switcher/MainActivity;->access$getAdapter$p(Lcom/dbdm/switcher/MainActivity;)Lcom/dbdm/switcher/ProfileAdapter;

    move-result-object v1

    if-nez v1, :cond_5

    const-string v1, "adapter"

    invoke-static {v1}, Lkotlin/jvm/internal/Intrinsics;->throwUninitializedPropertyAccessException(Ljava/lang/String;)V

    move-object v1, v3

    :cond_5
    iget-object v2, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v2}, Lcom/dbdm/switcher/MainActivity;->access$getStore$p(Lcom/dbdm/switcher/MainActivity;)Lcom/dbdm/switcher/ProfileStore;

    move-result-object v2

    if-nez v2, :cond_6

    const-string v2, "store"

    invoke-static {v2}, Lkotlin/jvm/internal/Intrinsics;->throwUninitializedPropertyAccessException(Ljava/lang/String;)V

    goto :goto_1

    :cond_6
    move-object v3, v2

    :goto_1
    invoke-virtual {v3}, Lcom/dbdm/switcher/ProfileStore;->load()Ljava/util/List;

    move-result-object v2

    iget-object v3, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    move-object v4, v2

    .line 369
    .local v4, "it":Ljava/util/List;
    const/4 v5, 0x0

    .line 110
    .local v5, "$i$a$-also-MainActivity$refresh$1$1$1":I
    invoke-static {v3}, Lcom/dbdm/switcher/MainActivity;->access$getProfiles$p(Lcom/dbdm/switcher/MainActivity;)Ljava/util/List;

    move-result-object v6

    invoke-interface {v6}, Ljava/util/List;->clear()V

    invoke-static {v3}, Lcom/dbdm/switcher/MainActivity;->access$getProfiles$p(Lcom/dbdm/switcher/MainActivity;)Ljava/util/List;

    move-result-object v3

    move-object v6, v4

    check-cast v6, Ljava/util/Collection;

    invoke-interface {v3, v6}, Ljava/util/List;->addAll(Ljava/util/Collection;)Z

    .end local v4    # "it":Ljava/util/List;
    .end local v5    # "$i$a$-also-MainActivity$refresh$1$1$1":I
    invoke-virtual {v1, v2}, Lcom/dbdm/switcher/ProfileAdapter;->replace(Ljava/util/List;)V

    .line 111
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$refresh$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    const/4 v2, 0x1

    const-string v3, "\u2713 Pronto"

    invoke-static {v1, v2, v3}, Lcom/dbdm/switcher/MainActivity;->access$setUi(Lcom/dbdm/switcher/MainActivity;ZLjava/lang/String;)V

    .line 112
    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    return-object v1

    :pswitch_data_0
    .packed-switch 0x0
        :pswitch_0
    .end packed-switch
.end method
