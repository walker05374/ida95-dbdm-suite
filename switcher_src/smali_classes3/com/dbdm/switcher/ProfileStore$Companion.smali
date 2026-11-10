.class public final Lcom/dbdm/switcher/ProfileStore$Companion;
.super Ljava/lang/Object;
.source "ProfileStore.kt"


# annotations
.annotation system Ldalvik/annotation/EnclosingClass;
    value = Lcom/dbdm/switcher/ProfileStore;
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x19
    name = "Companion"
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000\u0014\n\u0002\u0018\u0002\n\u0002\u0010\u0000\n\u0002\u0008\u0002\n\u0002\u0010\u000e\n\u0002\u0008\u0002\u0008\u0086\u0003\u0018\u00002\u00020\u0001B\u0007\u0008\u0002\u00a2\u0006\u0002\u0010\u0002J\u0012\u0010\u0003\u001a\u0004\u0018\u00010\u00042\u0008\u0010\u0005\u001a\u0004\u0018\u00010\u0004\u00a8\u0006\u0006"
    }
    d2 = {
        "Lcom/dbdm/switcher/ProfileStore$Companion;",
        "",
        "()V",
        "extractUserId",
        "",
        "accountsJson",
        "app_debug"
    }
    k = 0x1
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation


# direct methods
.method private constructor <init>()V
    .locals 0

    .line 53
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method

.method public synthetic constructor <init>(Lkotlin/jvm/internal/DefaultConstructorMarker;)V
    .locals 0

    invoke-direct {p0}, Lcom/dbdm/switcher/ProfileStore$Companion;-><init>()V

    return-void
.end method


# virtual methods
.method public final extractUserId(Ljava/lang/String;)Ljava/lang/String;
    .locals 4
    .param p1, "accountsJson"    # Ljava/lang/String;

    .line 56
    const/4 v0, 0x0

    if-nez p1, :cond_0

    return-object v0

    .line 57
    :cond_0
    nop

    .line 58
    :try_start_0
    new-instance v1, Lcom/google/gson/Gson;

    invoke-direct {v1}, Lcom/google/gson/Gson;-><init>()V

    .line 59
    new-instance v2, Lcom/dbdm/switcher/ProfileStore$Companion$extractUserId$map$1;

    invoke-direct {v2}, Lcom/dbdm/switcher/ProfileStore$Companion$extractUserId$map$1;-><init>()V

    invoke-virtual {v2}, Lcom/dbdm/switcher/ProfileStore$Companion$extractUserId$map$1;->getType()Ljava/lang/reflect/Type;

    move-result-object v2

    .line 58
    invoke-virtual {v1, p1, v2}, Lcom/google/gson/Gson;->fromJson(Ljava/lang/String;Ljava/lang/reflect/Type;)Ljava/lang/Object;

    move-result-object v1

    const-string v2, "fromJson(...)"

    invoke-static {v1, v2}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v1, Ljava/util/Map;

    .line 61
    .local v1, "map":Ljava/util/Map;
    invoke-interface {v1}, Ljava/util/Map;->values()Ljava/util/Collection;

    move-result-object v2

    check-cast v2, Ljava/lang/Iterable;

    invoke-static {v2}, Lkotlin/collections/CollectionsKt;->firstOrNull(Ljava/lang/Iterable;)Ljava/lang/Object;

    move-result-object v2

    instance-of v3, v2, Ljava/util/Map;

    if-eqz v3, :cond_1

    check-cast v2, Ljava/util/Map;

    goto :goto_0

    :cond_1
    move-object v2, v0

    :goto_0
    if-eqz v2, :cond_2

    const-string v3, "user_id"

    invoke-interface {v2, v3}, Ljava/util/Map;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v2

    goto :goto_1

    :cond_2
    move-object v2, v0

    :goto_1
    instance-of v3, v2, Ljava/lang/String;

    if-eqz v3, :cond_3

    check-cast v2, Ljava/lang/String;
    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_0

    move-object v0, v2

    goto :goto_2

    .line 62
    .end local v1    # "map":Ljava/util/Map;
    :catch_0
    move-exception v1

    .line 57
    :cond_3
    :goto_2
    return-object v0
.end method
